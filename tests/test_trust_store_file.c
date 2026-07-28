#include "qn_trust_store.h"

#include <stdio.h>
#include <string.h>

#define KEY_A \
    "000102030405060708090a0b0c0d0e0f" \
    "101112131415161718191a1b1c1d1e1f"

#define KEY_B \
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf" \
    "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"

#define ZERO_KEY \
    "00000000000000000000000000000000" \
    "00000000000000000000000000000000"

static const char valid_store[] =
    "QNTS1\n"
    "issuer\t" KEY_A "\tcreator primary\n"
    "issuer\t" KEY_B "\tcreator secondary\n";

static int expect_parse_failure(
    const uint8_t *data,
    size_t size,
    QNStatus expected_status,
    const char *expected_code
) {
    QNTrustStore store;
    QNDiagnostic diag = {0};

    qn_trust_store_init(&store);
    store.count = 17u;
    store.issuers[0].occupied = true;
    store.issuers[0].label[0] = 'X';

    QNStatus status = qn_trust_store_parse_text(
        data,
        size,
        &store,
        &diag
    );

    if (status != expected_status ||
        strcmp(diag.code, expected_code) != 0 ||
        store.count != 17u ||
        !store.issuers[0].occupied ||
        store.issuers[0].label[0] != 'X') {
        printf(
            "FAIL: EXPECTED_PARSE_FAILURE "
            "expected_code=%s actual_code=%s "
            "expected_status=%d actual_status=%d\n",
            expected_code,
            diag.code,
            (int)expected_status,
            (int)status
        );
        return 0;
    }

    return 1;
}

static void make_key(
    uint8_t key[QN_ED25519_PUBLIC_KEY_BYTES],
    uint8_t marker
) {
    for (size_t index = 0u;
         index < QN_ED25519_PUBLIC_KEY_BYTES;
         ++index) {
        key[index] = (uint8_t)(marker + index);
    }
}

static void encode_key_hex(
    const uint8_t key[QN_ED25519_PUBLIC_KEY_BYTES],
    char out[65]
) {
    static const char hex[] =
        "0123456789abcdef";

    for (size_t index = 0u;
         index < QN_ED25519_PUBLIC_KEY_BYTES;
         ++index) {
        out[index * 2u] =
            hex[key[index] >> 4];

        out[index * 2u + 1u] =
            hex[key[index] & 0x0fu];
    }

    out[64] = '\0';
}

int main(void) {
    QNTrustStore store;
    QNDiagnostic diag = {0};

    if (qn_trust_store_parse_text(
            (const uint8_t *)valid_store,
            strlen(valid_store),
            &store,
            &diag) != QN_OK ||
        store.count != 2u ||
        strcmp(
            store.issuers[0].label,
            "creator primary") != 0 ||
        strcmp(
            store.issuers[1].label,
            "creator secondary") != 0) {
        puts("FAIL: VALID_STORE_REJECTED");
        return 1;
    }

    uint8_t key_a[QN_ED25519_PUBLIC_KEY_BYTES];
    uint8_t fingerprint_a[QN_ED25519_FINGERPRINT_BYTES];
    uint8_t resolved[QN_ED25519_PUBLIC_KEY_BYTES];

    for (size_t index = 0u;
         index < QN_ED25519_PUBLIC_KEY_BYTES;
         ++index) {
        key_a[index] = (uint8_t)index;
    }

    qn_ed25519_fingerprint(key_a, fingerprint_a);

    if (qn_trust_store_resolve(
            &store,
            fingerprint_a,
            resolved,
            &diag) != QN_OK ||
        memcmp(resolved, key_a, sizeof(key_a)) != 0) {
        puts("FAIL: PARSED_KEY_RESOLUTION");
        return 1;
    }

    static const char empty_store[] = "QNTS1\n";

    if (qn_trust_store_parse_text(
            (const uint8_t *)empty_store,
            strlen(empty_store),
            &store,
            &diag) != QN_OK ||
        store.count != 0u) {
        puts("FAIL: EMPTY_STORE_REJECTED");
        return 1;
    }

    static const char bad_header[] = "BAD!!\n";
    if (!expect_parse_failure(
            (const uint8_t *)bad_header,
            strlen(bad_header),
            QN_ERR_RUNTIME,
            "QN-E5113")) {
        return 1;
    }

    static const char missing_newline[] = "QNTS1";
    if (!expect_parse_failure(
            (const uint8_t *)missing_newline,
            strlen(missing_newline),
            QN_ERR_RUNTIME,
            "QN-E5114")) {
        return 1;
    }

    static const char crlf[] = "QNTS1\r\n";
    if (!expect_parse_failure(
            (const uint8_t *)crlf,
            strlen(crlf),
            QN_ERR_RUNTIME,
            "QN-E5112")) {
        return 1;
    }

    static const char blank_line[] = "QNTS1\n\n";
    if (!expect_parse_failure(
            (const uint8_t *)blank_line,
            strlen(blank_line),
            QN_ERR_RUNTIME,
            "QN-E5114")) {
        return 1;
    }

    char uppercase[sizeof(valid_store)];
    memcpy(uppercase, valid_store, sizeof(valid_store));
    uppercase[13] = 'A';

    if (!expect_parse_failure(
            (const uint8_t *)uppercase,
            strlen(uppercase),
            QN_ERR_RUNTIME,
            "QN-E5115")) {
        return 1;
    }

    char invalid_hex[sizeof(valid_store)];
    memcpy(invalid_hex, valid_store, sizeof(valid_store));
    invalid_hex[13] = 'g';

    if (!expect_parse_failure(
            (const uint8_t *)invalid_hex,
            strlen(invalid_hex),
            QN_ERR_RUNTIME,
            "QN-E5115")) {
        return 1;
    }

    static const char short_key[] =
        "QNTS1\n"
        "issuer\t00\tshort\n";

    if (!expect_parse_failure(
            (const uint8_t *)short_key,
            strlen(short_key),
            QN_ERR_RUNTIME,
            "QN-E5114")) {
        return 1;
    }

    static const char duplicate[] =
        "QNTS1\n"
        "issuer\t" KEY_A "\tfirst\n"
        "issuer\t" KEY_A "\tsecond\n";

    if (!expect_parse_failure(
            (const uint8_t *)duplicate,
            strlen(duplicate),
            QN_ERR_RUNTIME,
            "QN-E5102")) {
        return 1;
    }

    static const char zero_key[] =
        "QNTS1\n"
        "issuer\t" ZERO_KEY "\tzero\n";

    if (!expect_parse_failure(
            (const uint8_t *)zero_key,
            strlen(zero_key),
            QN_ERR_RUNTIME,
            "QN-E5101")) {
        return 1;
    }

    char embedded_nul[sizeof(valid_store)];
    memcpy(embedded_nul, valid_store, sizeof(valid_store));
    embedded_nul[10] = '\0';

    if (!expect_parse_failure(
            (const uint8_t *)embedded_nul,
            sizeof(embedded_nul) - 1u,
            QN_ERR_RUNTIME,
            "QN-E5112")) {
        return 1;
    }

    uint8_t oversized[QN_TRUST_STORE_MAX_FILE_BYTES + 1u];
    memset(oversized, 'x', sizeof(oversized));

    if (!expect_parse_failure(
            oversized,
            sizeof(oversized),
            QN_ERR_LIMIT,
            "QN-E5111")) {
        return 1;
    }

    char capacity[QN_TRUST_STORE_MAX_FILE_BYTES];
    size_t used = (size_t)snprintf(
        capacity,
        sizeof(capacity),
        "QNTS1\n"
    );

    for (size_t index = 0u;
         index < QN_TRUST_STORE_MAX_ISSUERS + 1u;
         ++index) {
        uint8_t key[QN_ED25519_PUBLIC_KEY_BYTES];
        char key_hex[65];

        make_key(key, (uint8_t)(index + 1u));
        encode_key_hex(key, key_hex);

        int written = snprintf(
            capacity + used,
            sizeof(capacity) - used,
            "issuer\t%s\tissuer-%02zu\n",
            key_hex,
            index
        );

        if (written < 0 ||
            (size_t)written >= sizeof(capacity) - used) {
            puts("FAIL: CAPACITY_FIXTURE_OVERFLOW");
            return 1;
        }

        used += (size_t)written;
    }

    if (!expect_parse_failure(
            (const uint8_t *)capacity,
            used,
            QN_ERR_LIMIT,
            "QN-E5103")) {
        return 1;
    }

    const char *path =
        "build/test-trust-store-file.qnts";

    FILE *file = fopen(path, "wb");
    if (!file) {
        puts("FAIL: TEST_FILE_OPEN");
        return 1;
    }

    bool write_ok =
        fwrite(
            valid_store,
            1u,
            strlen(valid_store),
            file
        ) == strlen(valid_store);

    bool close_ok = fclose(file) == 0;

    if (!write_ok || !close_ok) {
        puts("FAIL: TEST_FILE_WRITE");
        return 1;
    }

    if (qn_trust_store_load_file(
            path,
            &store,
            &diag) != QN_OK ||
        store.count != 2u) {
        remove(path);
        puts("FAIL: VALID_FILE_LOAD");
        return 1;
    }

    remove(path);

    if (qn_trust_store_load_file(
            "build/does-not-exist.qnts",
            &store,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E5117") != 0) {
        puts("FAIL: MISSING_FILE_ACCEPTED");
        return 1;
    }

    if (qn_trust_store_parse_text(
            NULL,
            0u,
            &store,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5110") != 0) {
        puts("FAIL: NULL_DATA_ACCEPTED");
        return 1;
    }

    if (qn_trust_store_parse_text(
            (const uint8_t *)empty_store,
            strlen(empty_store),
            NULL,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5110") != 0) {
        puts("FAIL: NULL_OUTPUT_ACCEPTED");
        return 1;
    }

    puts(
        "PASS: "
        "QBIT_NOVA_TRUST_STORE_FILE_PARSER_"
        "V052_STEP2"
    );

    return 0;
}
