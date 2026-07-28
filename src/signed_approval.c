#include "qn_signed_approval.h"

#include <openssl/rand.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

#define QNAT_MAGIC "QNAT1"
#define QNAT_MAGIC_BYTES 5u
#define QNAT_PREFIX_FIXED_BYTES 139u
#define QNAT_SIGNATURE_BYTES QN_ED25519_SIGNATURE_BYTES
#define QNAT_MIN_BYTES \
    (QNAT_PREFIX_FIXED_BYTES + QNAT_SIGNATURE_BYTES)
#define QNAT_MAX_BYTES \
    (QNAT_MIN_BYTES + QN_SIGNED_APPROVAL_CONTEXT_MAX)

static const uint8_t QNAT_DOMAIN[QN_SIGNED_APPROVAL_DOMAIN_BYTES] = {
    'Q','B','I','T','-','N','O','V','A','-',
    'A','P','P','R','O','V','A','L','-',
    'E','D','2','5','5','1','9','-','V','0','5',
    0,0
};

static void put16be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void put32be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static void put64be(uint8_t *out, uint64_t value) {
    for (size_t i = 0; i < 8u; ++i) {
        out[i] = (uint8_t)(value >> (56u - i * 8u));
    }
}

static uint16_t get16be(const uint8_t *in) {
    return (uint16_t)(
        ((uint16_t)in[0] << 8) |
        (uint16_t)in[1]
    );
}

static uint32_t get32be(const uint8_t *in) {
    return
        ((uint32_t)in[0] << 24) |
        ((uint32_t)in[1] << 16) |
        ((uint32_t)in[2] << 8) |
        (uint32_t)in[3];
}

static uint64_t get64be(const uint8_t *in) {
    uint64_t value = 0u;
    for (size_t i = 0; i < 8u; ++i) {
        value = (value << 8) | in[i];
    }
    return value;
}

static bool constant_time_equal(
    const uint8_t *a,
    const uint8_t *b,
    size_t size
) {
    uint8_t difference = 0u;
    for (size_t i = 0; i < size; ++i) {
        difference |= (uint8_t)(a[i] ^ b[i]);
    }
    return difference == 0u;
}

bool qn_capability_to_stable_id(
    QNCapabilityMask capability,
    uint32_t *stable_id
) {
    if (!stable_id) return false;

    switch (capability) {
        case QN_CAP_QUANTUM_SIMULATE:
            *stable_id = UINT32_C(0x00000001);
            return true;
        case QN_CAP_EVIDENCE_EMIT:
            *stable_id = UINT32_C(0x00000002);
            return true;
        case QN_CAP_MODEL_EXEC:
            *stable_id = UINT32_C(0x00000100);
            return true;
        case QN_CAP_FILE_WRITE:
            *stable_id = UINT32_C(0x00000101);
            return true;
        case QN_CAP_NETWORK:
            *stable_id = UINT32_C(0x00000102);
            return true;
        case QN_CAP_DEVICE_CONTROL:
            *stable_id = UINT32_C(0x00000103);
            return true;
        case QN_CAP_SHELL_EXEC:
            *stable_id = UINT32_C(0x80000001);
            return true;
        default:
            return false;
    }
}

bool qn_capability_from_stable_id(
    uint32_t stable_id,
    QNCapabilityMask *capability
) {
    if (!capability) return false;

    switch (stable_id) {
        case UINT32_C(0x00000001):
            *capability = QN_CAP_QUANTUM_SIMULATE;
            return true;
        case UINT32_C(0x00000002):
            *capability = QN_CAP_EVIDENCE_EMIT;
            return true;
        case UINT32_C(0x00000100):
            *capability = QN_CAP_MODEL_EXEC;
            return true;
        case UINT32_C(0x00000101):
            *capability = QN_CAP_FILE_WRITE;
            return true;
        case UINT32_C(0x00000102):
            *capability = QN_CAP_NETWORK;
            return true;
        case UINT32_C(0x00000103):
            *capability = QN_CAP_DEVICE_CONTROL;
            return true;
        case UINT32_C(0x80000001):
            *capability = QN_CAP_SHELL_EXEC;
            return true;
        default:
            return false;
    }
}

static bool approval_eligible(QNCapabilityMask capability) {
    return capability != 0u &&
           (capability & (capability - 1u)) == 0u &&
           (capability & QN_CAP_APPROVAL_DEFAULT) != 0u &&
           (capability & QN_CAP_BLOCKED_DEFAULT) == 0u;
}

uint8_t *qn_signed_approval_load_key(
    const char *path,
    size_t expected_size,
    QNDiagnostic *diag
) {
    size_t size = 0u;
    uint8_t *key = qn_read_binary_file(path, &size, diag);
    if (!key) return NULL;

    if (size != expected_size) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "key '%s' must be exactly %zu bytes, got %zu",
            path,
            expected_size,
            size
        );
        free(key);
        return NULL;
    }

    return key;
}

static QNStatus write_private_key(
    const char *path,
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    QNDiagnostic *diag
) {
    if (!qn_write_binary_file(
            path,
            private_key,
            QN_ED25519_PRIVATE_KEY_BYTES,
            diag)) {
        return QN_ERR_IO;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "private key was written but chmod 0600 failed"
        );
        return QN_ERR_IO;
    }
#endif

    return QN_OK;
}

QNStatus qn_signed_approval_keypair_generate(
    const char *private_path,
    const char *public_path,
    QNDiagnostic *diag
) {
    uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES];
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];

    QNStatus status = qn_ed25519_keypair_generate(
        private_key,
        public_key,
        diag
    );

    if (status == QN_OK) {
        status = write_private_key(
            private_path,
            private_key,
            diag
        );
    }

    if (status == QN_OK &&
        !qn_write_binary_file(
            public_path,
            public_key,
            sizeof(public_key),
            diag)) {
        status = QN_ERR_IO;
    }

    qn_ed25519_wipe_private(private_key);
    return status;
}

QNStatus qn_signed_approval_derive_public(
    const char *private_path,
    const char *public_path,
    QNDiagnostic *diag
) {
    uint8_t *private_key = qn_signed_approval_load_key(
        private_path,
        QN_ED25519_PRIVATE_KEY_BYTES,
        diag
    );
    if (!private_key) return QN_ERR_IO;

    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];
    QNStatus status = qn_ed25519_public_from_private(
        private_key,
        public_key,
        diag
    );

    qn_ed25519_wipe_private(private_key);
    free(private_key);

    if (status == QN_OK &&
        !qn_write_binary_file(
            public_path,
            public_key,
            sizeof(public_key),
            diag)) {
        status = QN_ERR_IO;
    }

    return status;
}

static int hex_nibble(unsigned char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool qn_signed_approval_parse_nonce_hex(
    const char *text,
    uint8_t nonce[QN_SIGNED_APPROVAL_NONCE_BYTES]
) {
    if (!text ||
        strlen(text) != QN_SIGNED_APPROVAL_NONCE_BYTES * 2u) {
        return false;
    }

    for (size_t i = 0; i < QN_SIGNED_APPROVAL_NONCE_BYTES; ++i) {
        int high = hex_nibble((unsigned char)text[i * 2u]);
        int low = hex_nibble((unsigned char)text[i * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        nonce[i] = (uint8_t)((high << 4) | low);
    }

    return true;
}

static QNStatus encode_prefix(
    const QNSignedApprovalToken *token,
    uint8_t **data_out,
    size_t *size_out,
    QNDiagnostic *diag
) {
    if (token->context_size > QN_SIGNED_APPROVAL_CONTEXT_MAX) {
        qn_diag_set_code(
            diag,
            "QN-E5012",
            0,
            0,
            "signed approval context exceeds %u bytes",
            QN_SIGNED_APPROVAL_CONTEXT_MAX
        );
        return QN_ERR_RUNTIME;
    }

    size_t size =
        QNAT_PREFIX_FIXED_BYTES + token->context_size;
    uint8_t *data = calloc(size, 1u);

    if (!data) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "out of memory encoding signed approval"
        );
        return QN_ERR_RUNTIME;
    }

    size_t at = 0u;
    memcpy(data + at, QNAT_MAGIC, QNAT_MAGIC_BYTES);
    at += QNAT_MAGIC_BYTES;

    memcpy(
        data + at,
        QNAT_DOMAIN,
        QN_SIGNED_APPROVAL_DOMAIN_BYTES
    );
    at += QN_SIGNED_APPROVAL_DOMAIN_BYTES;

    memcpy(data + at, token->issuer_fingerprint, 32u);
    at += 32u;

    put32be(data + at, token->capability_id);
    at += 4u;

    memcpy(data + at, token->source_digest, 32u);
    at += 32u;

    put64be(data + at, token->issued_at);
    at += 8u;

    put64be(data + at, token->expires_at);
    at += 8u;

    memcpy(
        data + at,
        token->nonce,
        QN_SIGNED_APPROVAL_NONCE_BYTES
    );
    at += QN_SIGNED_APPROVAL_NONCE_BYTES;

    put16be(data + at, token->context_size);
    at += 2u;

    memcpy(data + at, token->context, token->context_size);
    at += token->context_size;

    if (at != size) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "internal signed approval size mismatch"
        );
        return QN_ERR_RUNTIME;
    }

    *data_out = data;
    *size_out = size;
    return QN_OK;
}

QNStatus qn_signed_approval_issue(
    QNCapabilityMask capability,
    QNCapabilityMask program_capabilities,
    const uint8_t source_digest[32],
    uint64_t issued_at,
    uint64_t expires_at,
    const uint8_t *nonce_or_null,
    const uint8_t *context,
    size_t context_size,
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    QNSignedApprovalToken *out,
    QNDiagnostic *diag
) {
    memset(out, 0, sizeof(*out));

    uint32_t stable_id = 0u;
    if (!qn_capability_to_stable_id(capability, &stable_id)) {
        qn_diag_set_code(
            diag,
            "QN-E5009",
            0,
            0,
            "capability has no stable token ID"
        );
        return QN_ERR_RUNTIME;
    }

    if (!approval_eligible(capability)) {
        qn_diag_set_code(
            diag,
            "QN-E5010",
            0,
            0,
            "capability '%s' is blocked or not approval-eligible",
            qn_capability_name(capability)
        );
        return QN_ERR_RUNTIME;
    }

    if ((program_capabilities & capability) == 0u) {
        qn_diag_set_code(
            diag,
            "QN-E5009",
            0,
            0,
            "program does not request capability '%s'",
            qn_capability_name(capability)
        );
        return QN_ERR_RUNTIME;
    }

    if (issued_at == 0u ||
        expires_at <= issued_at ||
        issued_at > (uint64_t)INT64_MAX ||
        expires_at > (uint64_t)INT64_MAX) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "signed approval time range is invalid"
        );
        return QN_ERR_RUNTIME;
    }

    if (context_size > QN_SIGNED_APPROVAL_CONTEXT_MAX ||
        (context_size != 0u && !context)) {
        qn_diag_set_code(
            diag,
            "QN-E5012",
            0,
            0,
            "signed approval context exceeds %u bytes",
            QN_SIGNED_APPROVAL_CONTEXT_MAX
        );
        return QN_ERR_RUNTIME;
    }

    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];
    QNStatus status = qn_ed25519_public_from_private(
        private_key,
        public_key,
        diag
    );
    if (status != QN_OK) return status;

    out->capability = capability;
    out->capability_id = stable_id;
    memcpy(out->source_digest, source_digest, 32u);
    out->issued_at = issued_at;
    out->expires_at = expires_at;
    out->context_size = (uint16_t)context_size;
    memcpy(out->context, context, context_size);

    qn_ed25519_fingerprint(
        public_key,
        out->issuer_fingerprint
    );

    if (nonce_or_null) {
        memcpy(
            out->nonce,
            nonce_or_null,
            QN_SIGNED_APPROVAL_NONCE_BYTES
        );
    } else {
        if (RAND_bytes(
                out->nonce,
                QN_SIGNED_APPROVAL_NONCE_BYTES) != 1) {
            qn_diag_set_code(
                diag,
                "QN-E5001",
                0,
                0,
                "cryptographic nonce generation failed"
            );
            return QN_ERR_RUNTIME;
        }
    }

    uint8_t *prefix = NULL;
    size_t prefix_size = 0u;
    status = encode_prefix(
        out,
        &prefix,
        &prefix_size,
        diag
    );

    if (status == QN_OK) {
        status = qn_ed25519_sign(
            private_key,
            prefix,
            prefix_size,
            out->signature,
            diag
        );
    }

    free(prefix);
    return status;
}

QNStatus qn_signed_approval_write(
    const char *path,
    const QNSignedApprovalToken *token,
    QNDiagnostic *diag
) {
    uint8_t *prefix = NULL;
    size_t prefix_size = 0u;
    QNStatus status = encode_prefix(
        token,
        &prefix,
        &prefix_size,
        diag
    );
    if (status != QN_OK) return status;

    size_t token_size =
        prefix_size + QN_ED25519_SIGNATURE_BYTES;
    uint8_t *data = malloc(token_size);
    if (!data) {
        free(prefix);
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "out of memory writing signed approval"
        );
        return QN_ERR_RUNTIME;
    }

    memcpy(data, prefix, prefix_size);
    memcpy(
        data + prefix_size,
        token->signature,
        QN_ED25519_SIGNATURE_BYTES
    );

    bool written = qn_write_binary_file(
        path,
        data,
        token_size,
        diag
    );

    free(data);
    free(prefix);
    return written ? QN_OK : QN_ERR_IO;
}

QNStatus qn_signed_approval_read(
    const char *path,
    QNSignedApprovalToken *out,
    QNDiagnostic *diag
) {
    memset(out, 0, sizeof(*out));

    size_t size = 0u;
    uint8_t *data = qn_read_binary_file(path, &size, diag);
    if (!data) return QN_ERR_IO;

    if (size < QNAT_MIN_BYTES || size > QNAT_MAX_BYTES) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "signed approval size is invalid"
        );
        return QN_ERR_RUNTIME;
    }

    if (memcmp(data, QNAT_MAGIC, QNAT_MAGIC_BYTES) != 0) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5005",
            0,
            0,
            "unsupported signed approval magic/version"
        );
        return QN_ERR_RUNTIME;
    }

    if (memcmp(
            data + QNAT_MAGIC_BYTES,
            QNAT_DOMAIN,
            QN_SIGNED_APPROVAL_DOMAIN_BYTES) != 0) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5005",
            0,
            0,
            "signed approval domain is unsupported"
        );
        return QN_ERR_RUNTIME;
    }

    size_t at =
        QNAT_MAGIC_BYTES + QN_SIGNED_APPROVAL_DOMAIN_BYTES;

    memcpy(out->issuer_fingerprint, data + at, 32u);
    at += 32u;

    out->capability_id = get32be(data + at);
    at += 4u;

    if (!qn_capability_from_stable_id(
            out->capability_id,
            &out->capability)) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5009",
            0,
            0,
            "signed approval capability ID is unknown"
        );
        return QN_ERR_RUNTIME;
    }

    memcpy(out->source_digest, data + at, 32u);
    at += 32u;

    out->issued_at = get64be(data + at);
    at += 8u;

    out->expires_at = get64be(data + at);
    at += 8u;

    memcpy(
        out->nonce,
        data + at,
        QN_SIGNED_APPROVAL_NONCE_BYTES
    );
    at += QN_SIGNED_APPROVAL_NONCE_BYTES;

    out->context_size = get16be(data + at);
    at += 2u;

    if (out->context_size > QN_SIGNED_APPROVAL_CONTEXT_MAX) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5012",
            0,
            0,
            "signed approval context length is oversized"
        );
        return QN_ERR_RUNTIME;
    }

    size_t expected_size =
        QNAT_PREFIX_FIXED_BYTES +
        out->context_size +
        QN_ED25519_SIGNATURE_BYTES;

    if (size != expected_size) {
        free(data);
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "signed approval is truncated or has trailing bytes"
        );
        return QN_ERR_RUNTIME;
    }

    memcpy(
        out->context,
        data + at,
        out->context_size
    );
    at += out->context_size;

    memcpy(
        out->signature,
        data + at,
        QN_ED25519_SIGNATURE_BYTES
    );

    qn_sha256(data, size, out->token_digest);
    free(data);
    return QN_OK;
}

QNStatus qn_signed_approval_verify(
    const QNSignedApprovalToken *token,
    const uint8_t expected_source_digest[32],
    QNCapabilityMask program_capabilities,
    uint64_t now,
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    QNCapabilityMask *approved,
    QNDiagnostic *diag
) {
    *approved = 0u;

    uint8_t fingerprint[32];
    qn_ed25519_fingerprint(public_key, fingerprint);

    if (!constant_time_equal(
            fingerprint,
            token->issuer_fingerprint,
            sizeof(fingerprint))) {
        qn_diag_set_code(
            diag,
            "QN-E5003",
            0,
            0,
            "issuer fingerprint does not match trusted public key"
        );
        return QN_ERR_RUNTIME;
    }

    uint8_t *prefix = NULL;
    size_t prefix_size = 0u;
    QNStatus status = encode_prefix(
        token,
        &prefix,
        &prefix_size,
        diag
    );
    if (status != QN_OK) return status;

    status = qn_ed25519_verify(
        public_key,
        prefix,
        prefix_size,
        token->signature,
        diag
    );
    free(prefix);

    if (status != QN_OK) return status;

    if (!approval_eligible(token->capability)) {
        qn_diag_set_code(
            diag,
            "QN-E5010",
            0,
            0,
            "valid signature cannot approve blocked capability '%s'",
            qn_capability_name(token->capability)
        );
        return QN_ERR_RUNTIME;
    }

    if (token->issued_at == 0u ||
        token->expires_at <= token->issued_at ||
        token->issued_at > (uint64_t)INT64_MAX ||
        token->expires_at > (uint64_t)INT64_MAX) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "signed approval contains invalid timestamps"
        );
        return QN_ERR_RUNTIME;
    }

    if (now < token->issued_at) {
        qn_diag_set_code(
            diag,
            "QN-E5007",
            0,
            0,
            "signed approval is not yet valid"
        );
        return QN_ERR_RUNTIME;
    }

    if (now > token->expires_at) {
        qn_diag_set_code(
            diag,
            "QN-E5006",
            0,
            0,
            "signed approval expired at %llu",
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
            "QN-E5008",
            0,
            0,
            "signed approval source scope does not match"
        );
        return QN_ERR_RUNTIME;
    }

    if ((program_capabilities & token->capability) == 0u) {
        qn_diag_set_code(
            diag,
            "QN-E5009",
            0,
            0,
            "signed approval capability is not requested by program"
        );
        return QN_ERR_RUNTIME;
    }

    *approved = token->capability;
    return QN_OK;
}
