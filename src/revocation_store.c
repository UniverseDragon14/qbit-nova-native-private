#define _POSIX_C_SOURCE 200809L

#include "qn_revocation_store.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define QNRV_HEADER "QNRV1\n"
#define QNRV_HEADER_BYTES 6u
#define QNRV_TOKEN_PREFIX "token\t"
#define QNRV_TOKEN_PREFIX_BYTES 6u
#define QNRV_ISSUER_PREFIX "issuer\t"
#define QNRV_ISSUER_PREFIX_BYTES 7u
#define QNRV_HEX_BYTES 64u

static bool constant_time_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
) {
    uint8_t difference = 0u;

    for (size_t index = 0u; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }

    return difference == 0u;
}

static bool digest_is_zero(
    const uint8_t digest[QN_REVOCATION_DIGEST_BYTES]
) {
    uint8_t combined = 0u;

    for (size_t index = 0u;
         index < QN_REVOCATION_DIGEST_BYTES;
         ++index) {
        combined |= digest[index];
    }

    return combined == 0u;
}

static int lower_hex_nibble(uint8_t character) {
    if (character >= (uint8_t)'0' &&
        character <= (uint8_t)'9') {
        return (int)(character - (uint8_t)'0');
    }

    if (character >= (uint8_t)'a' &&
        character <= (uint8_t)'f') {
        return (int)(character - (uint8_t)'a') + 10;
    }

    return -1;
}

static bool decode_digest(
    const uint8_t *text,
    uint8_t digest[QN_REVOCATION_DIGEST_BYTES]
) {
    for (size_t index = 0u;
         index < QN_REVOCATION_DIGEST_BYTES;
         ++index) {
        int high = lower_hex_nibble(text[index * 2u]);
        int low = lower_hex_nibble(text[index * 2u + 1u]);

        if (high < 0 || low < 0) {
            return false;
        }

        digest[index] = (uint8_t)((high << 4) | low);
    }

    return true;
}

static const QNRevocationEntry *find_entry(
    const QNRevocationStore *store,
    QNRevocationKind kind,
    const uint8_t digest[QN_REVOCATION_DIGEST_BYTES]
) {
    if (!store || !digest) {
        return NULL;
    }

    for (size_t index = 0u;
         index < QN_REVOCATION_MAX_ENTRIES;
         ++index) {
        const QNRevocationEntry *entry =
            &store->entries[index];

        if (entry->occupied &&
            entry->kind == kind &&
            constant_time_equal(
                entry->digest,
                digest,
                QN_REVOCATION_DIGEST_BYTES)) {
            return entry;
        }
    }

    return NULL;
}

static QNStatus add_entry(
    QNRevocationStore *store,
    QNRevocationKind kind,
    const uint8_t digest[QN_REVOCATION_DIGEST_BYTES],
    const char *reason,
    QNDiagnostic *diag
) {
    if (!store ||
        !digest ||
        !reason ||
        digest_is_zero(digest)) {
        qn_diag_set_code(
            diag,
            "QN-E6101",
            0,
            0,
            "invalid revocation entry"
        );
        return QN_ERR_RUNTIME;
    }

    if (find_entry(store, kind, digest)) {
        qn_diag_set_code(
            diag,
            "QN-E6104",
            0,
            0,
            "duplicate revocation entry"
        );
        return QN_ERR_RUNTIME;
    }

    if (store->count >= QN_REVOCATION_MAX_ENTRIES) {
        qn_diag_set_code(
            diag,
            "QN-E6105",
            0,
            0,
            "revocation store entry limit reached"
        );
        return QN_ERR_LIMIT;
    }

    for (size_t index = 0u;
         index < QN_REVOCATION_MAX_ENTRIES;
         ++index) {
        QNRevocationEntry *entry =
            &store->entries[index];

        if (!entry->occupied) {
            entry->occupied = true;
            entry->kind = kind;

            memcpy(
                entry->digest,
                digest,
                sizeof(entry->digest)
            );

            int written = snprintf(
                entry->reason,
                sizeof(entry->reason),
                "%s",
                reason
            );

            if (written < 0 ||
                (size_t)written >= sizeof(entry->reason)) {
                memset(entry, 0, sizeof(*entry));

                qn_diag_set_code(
                    diag,
                    "QN-E6101",
                    0,
                    0,
                    "invalid revocation reason"
                );

                return QN_ERR_RUNTIME;
            }

            ++store->count;
            return QN_OK;
        }
    }

    qn_diag_set_code(
        diag,
        "QN-E6105",
        0,
        0,
        "revocation store has no available slot"
    );

    return QN_ERR_LIMIT;
}

static void set_forbidden_byte_diag(
    QNDiagnostic *diag,
    const uint8_t *data,
    size_t offset
) {
    int line = 1;
    int column = 1;

    for (size_t index = 0u;
         index < offset;
         ++index) {
        if (data[index] == (uint8_t)'\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }

    qn_diag_set_code(
        diag,
        "QN-E6103",
        line,
        column,
        "revocation store contains forbidden byte 0x%02x",
        (unsigned int)data[offset]
    );
}

static bool pread_exact(
    int fd,
    uint8_t *data,
    size_t size
) {
    size_t done = 0u;

    while (done < size) {
        ssize_t amount = pread(
            fd,
            data + done,
            size - done,
            (off_t)done
        );

        if (amount < 0 && errno == EINTR) {
            continue;
        }

        if (amount <= 0) {
            return false;
        }

        done += (size_t)amount;
    }

    return true;
}

void qn_revocation_store_init(QNRevocationStore *store) {
    if (store) {
        memset(store, 0, sizeof(*store));
    }
}

QNStatus qn_revocation_store_parse_text(
    const uint8_t *data,
    size_t size,
    QNRevocationStore *out,
    QNDiagnostic *diag
) {
    if (!data || !out) {
        qn_diag_set_code(
            diag,
            "QN-E6101",
            0,
            0,
            "invalid revocation parser arguments"
        );
        return QN_ERR_RUNTIME;
    }

    if (size > QN_REVOCATION_MAX_FILE_BYTES) {
        qn_diag_set_code(
            diag,
            "QN-E6102",
            0,
            0,
            "revocation store exceeds %u bytes",
            QN_REVOCATION_MAX_FILE_BYTES
        );
        return QN_ERR_LIMIT;
    }

    for (size_t index = 0u; index < size; ++index) {
        if (data[index] == 0u ||
            data[index] == (uint8_t)'\r') {
            set_forbidden_byte_diag(
                diag,
                data,
                index
            );
            return QN_ERR_RUNTIME;
        }
    }

    if (size == 0u ||
        data[size - 1u] != (uint8_t)'\n') {
        qn_diag_set_code(
            diag,
            "QN-E6103",
            0,
            0,
            "revocation store must end with LF"
        );
        return QN_ERR_RUNTIME;
    }

    if (size < QNRV_HEADER_BYTES ||
        memcmp(
            data,
            QNRV_HEADER,
            QNRV_HEADER_BYTES
        ) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E6103",
            1,
            1,
            "invalid revocation header; expected QNRV1"
        );
        return QN_ERR_RUNTIME;
    }

    QNRevocationStore parsed;
    qn_revocation_store_init(&parsed);

    size_t offset = QNRV_HEADER_BYTES;
    int line_number = 2;

    while (offset < size) {
        size_t line_end = offset;

        while (line_end < size &&
               data[line_end] != (uint8_t)'\n') {
            ++line_end;
        }

        const uint8_t *line = data + offset;
        size_t line_size = line_end - offset;
        size_t prefix_size = 0u;
        QNRevocationKind kind = 0;

        if (line_size >= QNRV_TOKEN_PREFIX_BYTES &&
            memcmp(
                line,
                QNRV_TOKEN_PREFIX,
                QNRV_TOKEN_PREFIX_BYTES
            ) == 0) {
            kind = QN_REVOCATION_TOKEN;
            prefix_size = QNRV_TOKEN_PREFIX_BYTES;
        } else if (
            line_size >= QNRV_ISSUER_PREFIX_BYTES &&
            memcmp(
                line,
                QNRV_ISSUER_PREFIX,
                QNRV_ISSUER_PREFIX_BYTES
            ) == 0) {
            kind = QN_REVOCATION_ISSUER;
            prefix_size = QNRV_ISSUER_PREFIX_BYTES;
        } else {
            qn_diag_set_code(
                diag,
                "QN-E6103",
                line_number,
                1,
                "unknown revocation record type"
            );
            return QN_ERR_RUNTIME;
        }

        size_t reason_offset =
            prefix_size + QNRV_HEX_BYTES + 1u;

        if (line_size < reason_offset + 1u ||
            line[prefix_size + QNRV_HEX_BYTES] !=
                (uint8_t)'\t') {
            qn_diag_set_code(
                diag,
                "QN-E6103",
                line_number,
                1,
                "malformed revocation record"
            );
            return QN_ERR_RUNTIME;
        }

        uint8_t digest[QN_REVOCATION_DIGEST_BYTES];

        if (!decode_digest(
                line + prefix_size,
                digest)) {
            qn_diag_set_code(
                diag,
                "QN-E6103",
                line_number,
                (int)prefix_size + 1,
                "revocation digest must be exactly 64 "
                "lowercase hexadecimal characters"
            );
            return QN_ERR_RUNTIME;
        }

        if (digest_is_zero(digest)) {
            qn_diag_set_code(
                diag,
                "QN-E6101",
                line_number,
                (int)prefix_size + 1,
                "all-zero revocation digest is invalid"
            );
            return QN_ERR_RUNTIME;
        }

        size_t reason_size = line_size - reason_offset;

        if (reason_size == 0u ||
            reason_size >= QN_REVOCATION_REASON_MAX) {
            qn_diag_set_code(
                diag,
                "QN-E6103",
                line_number,
                (int)reason_offset + 1,
                "revocation reason must contain 1 to 63 "
                "printable ASCII characters"
            );
            return QN_ERR_RUNTIME;
        }

        char reason[QN_REVOCATION_REASON_MAX];

        for (size_t index = 0u;
             index < reason_size;
             ++index) {
            uint8_t character =
                line[reason_offset + index];

            if (character < 0x20u ||
                character > 0x7eu) {
                qn_diag_set_code(
                    diag,
                    "QN-E6103",
                    line_number,
                    (int)(reason_offset + index + 1u),
                    "revocation reason contains a "
                    "non-printable byte"
                );
                return QN_ERR_RUNTIME;
            }

            reason[index] = (char)character;
        }

        reason[reason_size] = '\0';

        QNDiagnostic entry_diag = {0};

        QNStatus status = add_entry(
            &parsed,
            kind,
            digest,
            reason,
            &entry_diag
        );

        if (status != QN_OK) {
            if (diag) {
                *diag = entry_diag;
                diag->line = line_number;
                diag->column = 1;
            }

            return status;
        }

        offset = line_end + 1u;
        ++line_number;
    }

    *out = parsed;
    return QN_OK;
}

QNStatus qn_revocation_store_load_file(
    const char *path,
    QNRevocationStore *out,
    QNDiagnostic *diag
) {
    if (!path || !out) {
        qn_diag_set_code(
            diag,
            "QN-E6101",
            0,
            0,
            "invalid revocation file arguments"
        );
        return QN_ERR_RUNTIME;
    }

    int fd;

    do {
        fd = open(
            path,
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW
        );
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        qn_diag_set_code(
            diag,
            "QN-E6106",
            0,
            0,
            "cannot open revocation store '%s': %s",
            path,
            strerror(errno)
        );
        return QN_ERR_IO;
    }

    struct stat metadata;

    if (fstat(fd, &metadata) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E6106",
            0,
            0,
            "cannot inspect revocation store '%s': %s",
            path,
            strerror(errno)
        );
        close(fd);
        return QN_ERR_IO;
    }

    if (!S_ISREG(metadata.st_mode) ||
        metadata.st_nlink != 1 ||
        (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E6106",
            0,
            0,
            "revocation store must be a single-link "
            "regular file without group/other write access"
        );
        close(fd);
        return QN_ERR_IO;
    }

    if (metadata.st_size <= 0 ||
        (uint64_t)metadata.st_size >
            (uint64_t)QN_REVOCATION_MAX_FILE_BYTES) {
        qn_diag_set_code(
            diag,
            metadata.st_size >
                (off_t)QN_REVOCATION_MAX_FILE_BYTES
                ? "QN-E6102"
                : "QN-E6103",
            0,
            0,
            "invalid revocation store size"
        );
        close(fd);

        return metadata.st_size >
            (off_t)QN_REVOCATION_MAX_FILE_BYTES
            ? QN_ERR_LIMIT
            : QN_ERR_RUNTIME;
    }

    size_t size = (size_t)metadata.st_size;
    uint8_t data[QN_REVOCATION_MAX_FILE_BYTES];

    if (!pread_exact(fd, data, size)) {
        qn_diag_set_code(
            diag,
            "QN-E6106",
            0,
            0,
            "short read from revocation store '%s'",
            path
        );
        close(fd);
        return QN_ERR_IO;
    }

    if (close(fd) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E6106",
            0,
            0,
            "cannot close revocation store '%s'",
            path
        );
        return QN_ERR_IO;
    }

    return qn_revocation_store_parse_text(
        data,
        size,
        out,
        diag
    );
}

bool qn_revocation_store_token_is_revoked(
    const QNRevocationStore *store,
    const uint8_t token_digest[QN_REVOCATION_DIGEST_BYTES]
) {
    return
        token_digest &&
        !digest_is_zero(token_digest) &&
        find_entry(
            store,
            QN_REVOCATION_TOKEN,
            token_digest
        ) != NULL;
}

bool qn_revocation_store_issuer_is_revoked(
    const QNRevocationStore *store,
    const uint8_t issuer_fingerprint[QN_REVOCATION_DIGEST_BYTES]
) {
    return
        issuer_fingerprint &&
        !digest_is_zero(issuer_fingerprint) &&
        find_entry(
            store,
            QN_REVOCATION_ISSUER,
            issuer_fingerprint
        ) != NULL;
}

QNStatus qn_revocation_store_check(
    const QNRevocationStore *store,
    const uint8_t token_digest[QN_REVOCATION_DIGEST_BYTES],
    const uint8_t issuer_fingerprint[QN_REVOCATION_DIGEST_BYTES],
    QNDiagnostic *diag
) {
    if (!store ||
        !token_digest ||
        !issuer_fingerprint ||
        digest_is_zero(token_digest) ||
        digest_is_zero(issuer_fingerprint)) {
        qn_diag_set_code(
            diag,
            "QN-E6101",
            0,
            0,
            "invalid revocation lookup arguments"
        );
        return QN_ERR_RUNTIME;
    }

    if (qn_revocation_store_token_is_revoked(
            store,
            token_digest)) {
        qn_diag_set_code(
            diag,
            "QN-E6107",
            0,
            0,
            "signed approval token is revoked"
        );
        return QN_ERR_RUNTIME;
    }

    if (qn_revocation_store_issuer_is_revoked(
            store,
            issuer_fingerprint)) {
        qn_diag_set_code(
            diag,
            "QN-E6108",
            0,
            0,
            "signed approval issuer is revoked"
        );
        return QN_ERR_RUNTIME;
    }

    return QN_OK;
}
