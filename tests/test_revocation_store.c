#define _POSIX_C_SOURCE 200809L

#include "qn_revocation_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TOKEN_A \
    "000102030405060708090a0b0c0d0e0f" \
    "101112131415161718191a1b1c1d1e1f"

#define ISSUER_A \
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf" \
    "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"

#define ZERO_DIGEST \
    "00000000000000000000000000000000" \
    "00000000000000000000000000000000"

static const char valid_store[] =
    "QNRV1\n"
    "token\t" TOKEN_A "\tcompromised token\n"
    "issuer\t" ISSUER_A "\tretired issuer\n";

static int hex_nibble(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }

    return -1;
}

static void decode_hex(
    const char text[64],
    uint8_t digest[QN_REVOCATION_DIGEST_BYTES]
) {
    for (size_t index = 0u;
         index < QN_REVOCATION_DIGEST_BYTES;
         ++index) {
        int high = hex_nibble(text[index * 2u]);
        int low = hex_nibble(text[index * 2u + 1u]);

        digest[index] =
            (uint8_t)((high << 4) | low);
    }
}

static void make_digest(
    uint8_t digest[QN_REVOCATION_DIGEST_BYTES],
    uint8_t marker
) {
    for (size_t index = 0u;
         index < QN_REVOCATION_DIGEST_BYTES;
         ++index) {
        digest[index] = (uint8_t)(marker + index);
    }
}

static void encode_hex(
    const uint8_t digest[QN_REVOCATION_DIGEST_BYTES],
    char out[65]
) {
    static const char hex[] =
        "0123456789abcdef";

    for (size_t index = 0u;
         index < QN_REVOCATION_DIGEST_BYTES;
         ++index) {
        out[index * 2u] =
            hex[digest[index] >> 4];

        out[index * 2u + 1u] =
            hex[digest[index] & 0x0fu];
    }

    out[64] = '\0';
}

static int expect_parse_failure(
    const uint8_t *data,
    size_t size,
    QNStatus expected_status,
    const char *expected_code
) {
    QNRevocationStore store;
    QNDiagnostic diag = {0};

    qn_revocation_store_init(&store);
    store.count = 17u;
    store.entries[0].occupied = true;
    store.entries[0].reason[0] = 'X';

    QNStatus status = qn_revocation_store_parse_text(
        data,
        size,
        &store,
        &diag
    );

    if (status != expected_status ||
        strcmp(diag.code, expected_code) != 0 ||
        store.count != 17u ||
        !store.entries[0].occupied ||
        store.entries[0].reason[0] != 'X') {
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

static int write_file(
    const char *path,
    const uint8_t *data,
    size_t size,
    mode_t mode
) {
    int fd = open(
        path,
        O_WRONLY | O_CREAT | O_TRUNC,
        mode
    );

    if (fd < 0) {
        return 0;
    }

    size_t done = 0u;

    while (done < size) {
        ssize_t amount = write(
            fd,
            data + done,
            size - done
        );

        if (amount < 0 && errno == EINTR) {
            continue;
        }

        if (amount <= 0) {
            close(fd);
            return 0;
        }

        done += (size_t)amount;
    }

    return close(fd) == 0;
}

int main(void) {
    QNRevocationStore store;
    QNDiagnostic diag = {0};

    uint8_t token_a[QN_REVOCATION_DIGEST_BYTES];
    uint8_t issuer_a[QN_REVOCATION_DIGEST_BYTES];
    uint8_t safe_token[QN_REVOCATION_DIGEST_BYTES];
    uint8_t safe_issuer[QN_REVOCATION_DIGEST_BYTES];

    decode_hex(TOKEN_A, token_a);
    decode_hex(ISSUER_A, issuer_a);
    make_digest(safe_token, 0x31u);
    make_digest(safe_issuer, 0x61u);

    if (qn_revocation_store_parse_text(
            (const uint8_t *)valid_store,
            strlen(valid_store),
            &store,
            &diag) != QN_OK ||
        store.count != 2u) {
        puts("FAIL: VALID_STORE_REJECTED");
        return 1;
    }

    if (!qn_revocation_store_token_is_revoked(
            &store,
            token_a) ||
        !qn_revocation_store_issuer_is_revoked(
            &store,
            issuer_a)) {
        puts("FAIL: REVOKED_ENTRY_NOT_FOUND");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_revocation_store_check(
            &store,
            token_a,
            safe_issuer,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E6107") != 0) {
        puts("FAIL: REVOKED_TOKEN_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_revocation_store_check(
            &store,
            safe_token,
            issuer_a,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E6108") != 0) {
        puts("FAIL: REVOKED_ISSUER_ACCEPTED");
        return 1;
    }

    if (qn_revocation_store_check(
            &store,
            safe_token,
            safe_issuer,
            &diag) != QN_OK) {
        puts("FAIL: SAFE_APPROVAL_REJECTED");
        return 1;
    }

    static const char empty_store[] = "QNRV1\n";

    if (qn_revocation_store_parse_text(
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
            "QN-E6103")) {
        return 1;
    }

    static const char crlf[] = "QNRV1\r\n";

    if (!expect_parse_failure(
            (const uint8_t *)crlf,
            strlen(crlf),
            QN_ERR_RUNTIME,
            "QN-E6103")) {
        return 1;
    }

    static const char blank_line[] = "QNRV1\n\n";

    if (!expect_parse_failure(
            (const uint8_t *)blank_line,
            strlen(blank_line),
            QN_ERR_RUNTIME,
            "QN-E6103")) {
        return 1;
    }

    static const char unknown_type[] =
        "QNRV1\n"
        "other\t" TOKEN_A "\treason\n";

    if (!expect_parse_failure(
            (const uint8_t *)unknown_type,
            strlen(unknown_type),
            QN_ERR_RUNTIME,
            "QN-E6103")) {
        return 1;
    }

    static const char short_digest[] =
        "QNRV1\n"
        "token\t00\tshort\n";

    if (!expect_parse_failure(
            (const uint8_t *)short_digest,
            strlen(short_digest),
            QN_ERR_RUNTIME,
            "QN-E6103")) {
        return 1;
    }

    char uppercase[sizeof(valid_store)];
    memcpy(uppercase, valid_store, sizeof(valid_store));
    uppercase[12] = 'A';

    if (!expect_parse_failure(
            (const uint8_t *)uppercase,
            strlen(uppercase),
            QN_ERR_RUNTIME,
            "QN-E6103")) {
        return 1;
    }

    static const char zero_digest[] =
        "QNRV1\n"
        "token\t" ZERO_DIGEST "\tzero\n";

    if (!expect_parse_failure(
            (const uint8_t *)zero_digest,
            strlen(zero_digest),
            QN_ERR_RUNTIME,
            "QN-E6101")) {
        return 1;
    }

    static const char duplicate[] =
        "QNRV1\n"
        "token\t" TOKEN_A "\tfirst\n"
        "token\t" TOKEN_A "\tsecond\n";

    if (!expect_parse_failure(
            (const uint8_t *)duplicate,
            strlen(duplicate),
            QN_ERR_RUNTIME,
            "QN-E6104")) {
        return 1;
    }

    char embedded_nul[sizeof(valid_store)];
    memcpy(
        embedded_nul,
        valid_store,
        sizeof(valid_store)
    );
    embedded_nul[10] = '\0';

    if (!expect_parse_failure(
            (const uint8_t *)embedded_nul,
            sizeof(embedded_nul) - 1u,
            QN_ERR_RUNTIME,
            "QN-E6103")) {
        return 1;
    }

    uint8_t oversized[
        QN_REVOCATION_MAX_FILE_BYTES + 1u
    ];

    memset(oversized, 'x', sizeof(oversized));

    if (!expect_parse_failure(
            oversized,
            sizeof(oversized),
            QN_ERR_LIMIT,
            "QN-E6102")) {
        return 1;
    }

    char capacity[QN_REVOCATION_MAX_FILE_BYTES];
    size_t used = (size_t)snprintf(
        capacity,
        sizeof(capacity),
        "QNRV1\n"
    );

    for (size_t index = 0u;
         index < QN_REVOCATION_MAX_ENTRIES + 1u;
         ++index) {
        uint8_t digest[QN_REVOCATION_DIGEST_BYTES];
        char hex[65];

        make_digest(
            digest,
            (uint8_t)(index + 1u)
        );
        encode_hex(digest, hex);

        int written = snprintf(
            capacity + used,
            sizeof(capacity) - used,
            "token\t%s\tentry-%02zu\n",
            hex,
            index
        );

        if (written < 0 ||
            (size_t)written >=
                sizeof(capacity) - used) {
            puts("FAIL: CAPACITY_FIXTURE_OVERFLOW");
            return 1;
        }

        used += (size_t)written;
    }

    if (!expect_parse_failure(
            (const uint8_t *)capacity,
            used,
            QN_ERR_LIMIT,
            "QN-E6105")) {
        return 1;
    }

    char directory_template[] =
        "/tmp/qnova-revocation-XXXXXX";

    char *directory = mkdtemp(directory_template);

    if (!directory) {
        puts("FAIL: TEMP_DIRECTORY");
        return 1;
    }

    char valid_path[QN_PATH_CAP];
    char unsafe_path[QN_PATH_CAP];
    char symlink_path[QN_PATH_CAP];

    if (snprintf(
            valid_path,
            sizeof(valid_path),
            "%s/valid.qnrv",
            directory) < 0 ||
        snprintf(
            unsafe_path,
            sizeof(unsafe_path),
            "%s/unsafe.qnrv",
            directory) < 0 ||
        snprintf(
            symlink_path,
            sizeof(symlink_path),
            "%s/link.qnrv",
            directory) < 0) {
        puts("FAIL: PATH_BUILD");
        return 1;
    }

    if (!write_file(
            valid_path,
            (const uint8_t *)valid_store,
            strlen(valid_store),
            S_IRUSR | S_IWUSR)) {
        puts("FAIL: VALID_FILE_FIXTURE");
        return 1;
    }

    if (qn_revocation_store_load_file(
            valid_path,
            &store,
            &diag) != QN_OK ||
        store.count != 2u) {
        puts("FAIL: VALID_FILE_LOAD");
        return 1;
    }

    if (symlink(valid_path, symlink_path) != 0) {
        puts("FAIL: SYMLINK_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_revocation_store_load_file(
            symlink_path,
            &store,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E6106") != 0) {
        puts("FAIL: SYMLINK_STORE_ACCEPTED");
        return 1;
    }

    if (!write_file(
            unsafe_path,
            (const uint8_t *)valid_store,
            strlen(valid_store),
            S_IRUSR | S_IWUSR |
                S_IWGRP | S_IWOTH) ||
        chmod(
            unsafe_path,
            S_IRUSR | S_IWUSR |
                S_IWGRP | S_IWOTH) != 0) {
        puts("FAIL: UNSAFE_MODE_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_revocation_store_load_file(
            unsafe_path,
            &store,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E6106") != 0) {
        puts("FAIL: UNSAFE_MODE_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_revocation_store_load_file(
            "/tmp/qnova-revocation-does-not-exist",
            &store,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E6106") != 0) {
        puts("FAIL: MISSING_STORE_ACCEPTED");
        return 1;
    }

    (void)unlink(symlink_path);
    (void)unlink(unsafe_path);
    (void)unlink(valid_path);
    (void)rmdir(directory);

    puts(
        "PASS: "
        "QBIT_NOVA_REVOCATION_STORE_FOUNDATION_"
        "V052_STEP6"
    );

    return 0;
}
