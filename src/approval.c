#include "qn_approval.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define APPROVAL_MARKER "QBIT_NOVA_APPROVAL_V04"
#define APPROVAL_PAYLOAD_CAP 768u
#define APPROVAL_FILE_CAP 1024u

static bool constant_time_equal(const uint8_t *a,
                                const uint8_t *b,
                                size_t size) {
    uint8_t difference = 0u;
    for (size_t i = 0; i < size; ++i) {
        difference |= (uint8_t)(a[i] ^ b[i]);
    }
    return difference == 0u;
}

static bool decode_hex32(const char *text, uint8_t out[32]) {
    if (!text || strlen(text) != 64u) return false;

    for (size_t i = 0; i < 32u; ++i) {
        int high;
        int low;
        unsigned char a = (unsigned char)text[i * 2u];
        unsigned char b = (unsigned char)text[i * 2u + 1u];

        if (a >= '0' && a <= '9') high = a - '0';
        else if (a >= 'a' && a <= 'f') high = a - 'a' + 10;
        else if (a >= 'A' && a <= 'F') high = a - 'A' + 10;
        else return false;

        if (b >= '0' && b <= '9') low = b - '0';
        else if (b >= 'a' && b <= 'f') low = b - 'a' + 10;
        else if (b >= 'A' && b <= 'F') low = b - 'A' + 10;
        else return false;

        out[i] = (uint8_t)((high << 4) | low);
    }

    return true;
}

static bool nonce_valid(const char *nonce) {
    if (!nonce) return false;

    size_t length = strlen(nonce);
    if (length == 0u || length >= QN_APPROVAL_NONCE_CAP) {
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)nonce[i];
        if (!(isalnum(c) || c == '_' || c == '-')) {
            return false;
        }
    }

    return true;
}

static bool build_payload(const QNApprovalToken *token,
                          char payload[APPROVAL_PAYLOAD_CAP],
                          size_t *payload_size,
                          QNDiagnostic *diag) {
    char source_hex[65];
    qn_hex32(token->source_digest, source_hex);

    int written = snprintf(
        payload,
        APPROVAL_PAYLOAD_CAP,
        "%s\n"
        "version=%u\n"
        "capability=%s\n"
        "source_sha256=%s\n"
        "issued_at=%llu\n"
        "expires_at=%llu\n"
        "nonce=%s\n",
        APPROVAL_MARKER,
        token->version,
        token->capability_name,
        source_hex,
        (unsigned long long)token->issued_at,
        (unsigned long long)token->expires_at,
        token->nonce
    );

    if (written < 0 ||
        (size_t)written >= APPROVAL_PAYLOAD_CAP) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-008",
            0,
            0,
            "approval payload exceeds internal limit"
        );
        return false;
    }

    *payload_size = (size_t)written;
    return true;
}

static void hmac_sha256(const uint8_t *key,
                        size_t key_size,
                        const uint8_t *message,
                        size_t message_size,
                        uint8_t out[32]) {
    uint8_t key_block[64] = {0};
    uint8_t normalized[32];

    if (key_size > sizeof(key_block)) {
        qn_sha256(key, key_size, normalized);
        memcpy(key_block, normalized, sizeof(normalized));
    } else {
        memcpy(key_block, key, key_size);
    }

    uint8_t inner_pad[64];
    uint8_t outer_pad[64];

    for (size_t i = 0; i < 64u; ++i) {
        inner_pad[i] = (uint8_t)(key_block[i] ^ 0x36u);
        outer_pad[i] = (uint8_t)(key_block[i] ^ 0x5cu);
    }

    size_t inner_size = sizeof(inner_pad) + message_size;
    uint8_t *inner = malloc(inner_size);
    if (!inner) {
        memset(out, 0, 32);
        return;
    }

    memcpy(inner, inner_pad, sizeof(inner_pad));
    memcpy(inner + sizeof(inner_pad), message, message_size);

    uint8_t inner_digest[32];
    qn_sha256(inner, inner_size, inner_digest);
    free(inner);

    uint8_t outer[96];
    memcpy(outer, outer_pad, sizeof(outer_pad));
    memcpy(outer + sizeof(outer_pad),
           inner_digest,
           sizeof(inner_digest));

    qn_sha256(outer, sizeof(outer), out);
}

static bool key_size_valid(size_t key_size,
                           QNDiagnostic *diag) {
    if (key_size < QN_APPROVAL_KEY_MIN ||
        key_size > QN_APPROVAL_KEY_MAX) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-007",
            0,
            0,
            "approval key size must be %u..%u bytes",
            QN_APPROVAL_KEY_MIN,
            QN_APPROVAL_KEY_MAX
        );
        return false;
    }
    return true;
}

uint8_t *qn_approval_load_key(const char *path,
                              size_t *key_size,
                              QNDiagnostic *diag) {
    size_t size = 0;
    uint8_t *key = qn_read_binary_file(path, &size, diag);
    if (!key) return NULL;

    if (!key_size_valid(size, diag)) {
        free(key);
        return NULL;
    }

    *key_size = size;
    return key;
}

static bool capability_can_be_approved(
    QNCapabilityMask capability
) {
    return capability != 0u &&
           (capability & (capability - 1u)) == 0u &&
           (capability & QN_CAP_APPROVAL_DEFAULT) != 0u;
}

static void derive_nonce(QNCapabilityMask capability,
                         const uint8_t source_digest[32],
                         uint64_t issued_at,
                         uint64_t expires_at,
                         char out[QN_APPROVAL_NONCE_CAP]) {
    char source_hex[65];
    char material[256];
    uint8_t digest[32];
    char digest_hex[65];

    qn_hex32(source_digest, source_hex);

    int written = snprintf(
        material,
        sizeof(material),
        "%s|%s|%llu|%llu",
        qn_capability_name(capability),
        source_hex,
        (unsigned long long)issued_at,
        (unsigned long long)expires_at
    );

    if (written < 0) {
        out[0] = '\0';
        return;
    }

    size_t size = (size_t)written;
    if (size >= sizeof(material)) size = sizeof(material) - 1u;

    qn_sha256((const uint8_t *)material, size, digest);
    qn_hex32(digest, digest_hex);

    memcpy(out, digest_hex, 32u);
    out[32] = '\0';
}

QNStatus qn_approval_create(QNCapabilityMask capability,
                            const uint8_t source_digest[32],
                            uint64_t issued_at,
                            uint64_t expires_at,
                            const char *nonce,
                            const uint8_t *key,
                            size_t key_size,
                            QNApprovalToken *out,
                            QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));

    if (!capability_can_be_approved(capability)) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-006",
            0,
            0,
            "capability '%s' is not approval-eligible",
            qn_capability_name(capability)
        );
        return QN_ERR_RUNTIME;
    }

    if (!key || !key_size_valid(key_size, diag)) {
        return QN_ERR_RUNTIME;
    }

    if (issued_at == 0u ||
        expires_at == 0u ||
        expires_at <= issued_at) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-002",
            0,
            0,
            "approval time range is invalid"
        );
        return QN_ERR_RUNTIME;
    }

    out->version = 1u;
    out->capability = capability;
    snprintf(
        out->capability_name,
        sizeof(out->capability_name),
        "%s",
        qn_capability_name(capability)
    );
    memcpy(out->source_digest, source_digest, 32);
    out->issued_at = issued_at;
    out->expires_at = expires_at;

    if (nonce && nonce[0]) {
        snprintf(out->nonce, sizeof(out->nonce), "%s", nonce);
    } else {
        derive_nonce(
            capability,
            source_digest,
            issued_at,
            expires_at,
            out->nonce
        );
    }

    if (!nonce_valid(out->nonce)) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-009",
            0,
            0,
            "nonce must contain 1..64 letters, numbers, '_' or '-'"
        );
        return QN_ERR_RUNTIME;
    }

    char payload[APPROVAL_PAYLOAD_CAP];
    size_t payload_size = 0;
    if (!build_payload(out, payload, &payload_size, diag)) {
        return QN_ERR_RUNTIME;
    }

    hmac_sha256(
        key,
        key_size,
        (const uint8_t *)payload,
        payload_size,
        out->mac
    );

    return QN_OK;
}

QNStatus qn_approval_write(const char *path,
                           const QNApprovalToken *token,
                           QNDiagnostic *diag) {
    char payload[APPROVAL_PAYLOAD_CAP];
    size_t payload_size = 0;
    if (!build_payload(token, payload, &payload_size, diag)) {
        return QN_ERR_IO;
    }

    char mac_hex[65];
    qn_hex32(token->mac, mac_hex);

    char file_data[APPROVAL_FILE_CAP];
    int written = snprintf(
        file_data,
        sizeof(file_data),
        "%smac_sha256=%s\n",
        payload,
        mac_hex
    );

    if (written < 0 ||
        (size_t)written >= sizeof(file_data)) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-008",
            0,
            0,
            "approval token exceeds internal limit"
        );
        return QN_ERR_IO;
    }

    if (!qn_write_binary_file(
            path,
            (const uint8_t *)file_data,
            (size_t)written,
            diag)) {
        return QN_ERR_IO;
    }

    return QN_OK;
}

static bool take_line(char **cursor,
                      char *out,
                      size_t out_size) {
    if (!cursor || !*cursor || !**cursor) return false;

    char *start = *cursor;
    char *newline = strchr(start, '\n');
    size_t length = newline
        ? (size_t)(newline - start)
        : strlen(start);

    if (length > 0u && start[length - 1u] == '\r') {
        --length;
    }

    if (length >= out_size) return false;

    memcpy(out, start, length);
    out[length] = '\0';

    *cursor = newline ? newline + 1 : start + strlen(start);
    return true;
}

static bool parse_exact_field(const char *line,
                              const char *prefix,
                              char *value,
                              size_t value_size) {
    size_t prefix_size = strlen(prefix);
    if (strncmp(line, prefix, prefix_size) != 0) return false;

    const char *source = line + prefix_size;
    size_t length = strlen(source);
    if (length == 0u || length >= value_size) return false;

    memcpy(value, source, length + 1u);
    return true;
}

static bool parse_u64_field(const char *line,
                            const char *prefix,
                            uint64_t *value) {
    char text[32];
    if (!parse_exact_field(line, prefix, text, sizeof(text))) {
        return false;
    }

    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (!end || *end != '\0') return false;

    *value = (uint64_t)parsed;
    return true;
}

QNStatus qn_approval_read(const char *path,
                          QNApprovalToken *out,
                          QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));

    size_t size = 0;
    char *file_data = qn_read_text_file(path, &size, diag);
    if (!file_data) return QN_ERR_IO;

    qn_sha256(
        (const uint8_t *)file_data,
        size,
        out->token_digest
    );

    char *cursor = file_data;
    char line[256];
    char version_text[16];
    char source_hex[65];
    char mac_hex[65];

    bool ok =
        take_line(&cursor, line, sizeof(line)) &&
        strcmp(line, APPROVAL_MARKER) == 0 &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_exact_field(
            line,
            "version=",
            version_text,
            sizeof(version_text)
        ) &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_exact_field(
            line,
            "capability=",
            out->capability_name,
            sizeof(out->capability_name)
        ) &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_exact_field(
            line,
            "source_sha256=",
            source_hex,
            sizeof(source_hex)
        ) &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_u64_field(
            line,
            "issued_at=",
            &out->issued_at
        ) &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_u64_field(
            line,
            "expires_at=",
            &out->expires_at
        ) &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_exact_field(
            line,
            "nonce=",
            out->nonce,
            sizeof(out->nonce)
        ) &&

        take_line(&cursor, line, sizeof(line)) &&
        parse_exact_field(
            line,
            "mac_sha256=",
            mac_hex,
            sizeof(mac_hex)
        );

    if (!ok || cursor[0] != '\0') {
        free(file_data);
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-010",
            0,
            0,
            "approval token format is invalid"
        );
        return QN_ERR_RUNTIME;
    }

    char *end = NULL;
    unsigned long version = strtoul(version_text, &end, 10);
    if (!end || *end != '\0' || version != 1u) {
        free(file_data);
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-010",
            0,
            0,
            "unsupported approval token version"
        );
        return QN_ERR_RUNTIME;
    }

    out->version = (uint32_t)version;

    if (!qn_capability_parse(
            out->capability_name,
            &out->capability) ||
        !decode_hex32(source_hex, out->source_digest) ||
        !decode_hex32(mac_hex, out->mac) ||
        !nonce_valid(out->nonce)) {
        free(file_data);
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-010",
            0,
            0,
            "approval token fields are invalid"
        );
        return QN_ERR_RUNTIME;
    }

    free(file_data);
    return QN_OK;
}

QNStatus qn_approval_verify(const QNApprovalToken *token,
                            const uint8_t expected_source_digest[32],
                            uint64_t now,
                            const uint8_t *key,
                            size_t key_size,
                            QNCapabilityMask *approved,
                            QNDiagnostic *diag) {
    *approved = 0u;

    if (!capability_can_be_approved(token->capability)) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-006",
            0,
            0,
            "token capability '%s' is not approval-eligible",
            token->capability_name
        );
        return QN_ERR_RUNTIME;
    }

    if (!key || !key_size_valid(key_size, diag)) {
        return QN_ERR_RUNTIME;
    }

    if (token->issued_at == 0u ||
        token->expires_at <= token->issued_at ||
        now < token->issued_at) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-002",
            0,
            0,
            "approval token is not active yet or has invalid time scope"
        );
        return QN_ERR_RUNTIME;
    }

    if (now > token->expires_at) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-005",
            0,
            0,
            "approval token expired at %llu",
            (unsigned long long)token->expires_at
        );
        return QN_ERR_RUNTIME;
    }

    if (!constant_time_equal(
            token->source_digest,
            expected_source_digest,
            32u)) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-003",
            0,
            0,
            "approval token source scope does not match"
        );
        return QN_ERR_RUNTIME;
    }

    char payload[APPROVAL_PAYLOAD_CAP];
    size_t payload_size = 0;
    if (!build_payload(token, payload, &payload_size, diag)) {
        return QN_ERR_RUNTIME;
    }

    uint8_t expected_mac[32];
    hmac_sha256(
        key,
        key_size,
        (const uint8_t *)payload,
        payload_size,
        expected_mac
    );

    if (!constant_time_equal(
            token->mac,
            expected_mac,
            sizeof(expected_mac))) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-004",
            0,
            0,
            "approval token authentication failed"
        );
        return QN_ERR_RUNTIME;
    }

    *approved = token->capability;
    return QN_OK;
}
