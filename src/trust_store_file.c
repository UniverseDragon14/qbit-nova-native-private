#include "qn_trust_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define QNTS_HEADER "QNTS1\n"
#define QNTS_HEADER_BYTES 6u
#define QNTS_RECORD_PREFIX "issuer\t"
#define QNTS_RECORD_PREFIX_BYTES 7u
#define QNTS_PUBLIC_KEY_HEX_BYTES 64u
#define QNTS_LABEL_OFFSET \
    (QNTS_RECORD_PREFIX_BYTES + \
     QNTS_PUBLIC_KEY_HEX_BYTES + 1u)

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

static bool decode_public_key(
    const uint8_t *text,
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES]
) {
    for (size_t index = 0u;
         index < QN_ED25519_PUBLIC_KEY_BYTES;
         ++index) {
        int high = lower_hex_nibble(text[index * 2u]);
        int low = lower_hex_nibble(text[index * 2u + 1u]);

        if (high < 0 || low < 0) {
            return false;
        }

        public_key[index] =
            (uint8_t)((high << 4) | low);
    }

    return true;
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
        "QN-E5112",
        line,
        column,
        "trust store contains forbidden byte 0x%02x",
        (unsigned int)data[offset]
    );
}

QNStatus qn_trust_store_parse_text(
    const uint8_t *data,
    size_t size,
    QNTrustStore *out,
    QNDiagnostic *diag
) {
    if (!data || !out) {
        qn_diag_set_code(
            diag,
            "QN-E5110",
            0,
            0,
            "invalid trust store parser arguments"
        );
        return QN_ERR_RUNTIME;
    }

    if (size > QN_TRUST_STORE_MAX_FILE_BYTES) {
        qn_diag_set_code(
            diag,
            "QN-E5111",
            0,
            0,
            "trust store exceeds %u bytes",
            QN_TRUST_STORE_MAX_FILE_BYTES
        );
        return QN_ERR_LIMIT;
    }

    for (size_t index = 0u;
         index < size;
         ++index) {
        if (data[index] == 0u ||
            data[index] == (uint8_t)'\r') {
            set_forbidden_byte_diag(diag, data, index);
            return QN_ERR_RUNTIME;
        }
    }

    if (size == 0u ||
        data[size - 1u] != (uint8_t)'\n') {
        qn_diag_set_code(
            diag,
            "QN-E5114",
            0,
            0,
            "trust store must end with a newline"
        );
        return QN_ERR_RUNTIME;
    }

    if (size < QNTS_HEADER_BYTES ||
        memcmp(data, QNTS_HEADER, QNTS_HEADER_BYTES) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5113",
            1,
            1,
            "invalid trust store header; expected QNTS1"
        );
        return QN_ERR_RUNTIME;
    }

    QNTrustStore parsed;
    qn_trust_store_init(&parsed);

    size_t offset = QNTS_HEADER_BYTES;
    int line_number = 2;

    while (offset < size) {
        size_t line_end = offset;

        while (line_end < size &&
               data[line_end] != (uint8_t)'\n') {
            ++line_end;
        }

        size_t line_size = line_end - offset;
        const uint8_t *line = data + offset;

        if (line_size < QNTS_LABEL_OFFSET + 1u ||
            memcmp(
                line,
                QNTS_RECORD_PREFIX,
                QNTS_RECORD_PREFIX_BYTES
            ) != 0 ||
            line[QNTS_LABEL_OFFSET - 1u] !=
                (uint8_t)'\t') {
            qn_diag_set_code(
                diag,
                "QN-E5114",
                line_number,
                1,
                "malformed trust store issuer record"
            );
            return QN_ERR_RUNTIME;
        }

        uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];

        if (!decode_public_key(
                line + QNTS_RECORD_PREFIX_BYTES,
                public_key)) {
            qn_diag_set_code(
                diag,
                "QN-E5115",
                line_number,
                (int)QNTS_RECORD_PREFIX_BYTES + 1,
                "public key must be exactly 64 "
                "lowercase hexadecimal characters"
            );
            return QN_ERR_RUNTIME;
        }

        size_t label_size =
            line_size - QNTS_LABEL_OFFSET;

        if (label_size == 0u ||
            label_size >= QN_TRUST_STORE_LABEL_MAX) {
            qn_diag_set_code(
                diag,
                "QN-E5116",
                line_number,
                (int)QNTS_LABEL_OFFSET + 1,
                "issuer label must contain 1 to 63 "
                "printable ASCII characters"
            );
            return QN_ERR_RUNTIME;
        }

        char label[QN_TRUST_STORE_LABEL_MAX];

        for (size_t index = 0u;
             index < label_size;
             ++index) {
            uint8_t character =
                line[QNTS_LABEL_OFFSET + index];

            if (character < 0x20u ||
                character > 0x7eu) {
                qn_diag_set_code(
                    diag,
                    "QN-E5116",
                    line_number,
                    (int)(QNTS_LABEL_OFFSET + index + 1u),
                    "issuer label contains a "
                    "non-printable byte"
                );
                return QN_ERR_RUNTIME;
            }

            label[index] = (char)character;
        }

        label[label_size] = '\0';

        QNDiagnostic entry_diag = {0};

        QNStatus status = qn_trust_store_add(
            &parsed,
            public_key,
            label,
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

QNStatus qn_trust_store_load_file(
    const char *path,
    QNTrustStore *out,
    QNDiagnostic *diag
) {
    if (!path || !out) {
        qn_diag_set_code(
            diag,
            "QN-E5110",
            0,
            0,
            "invalid trust store file arguments"
        );
        return QN_ERR_RUNTIME;
    }

    FILE *file = fopen(path, "rb");

    if (!file) {
        qn_diag_set_code(
            diag,
            "QN-E5117",
            0,
            0,
            "cannot open trust store '%s': %s",
            path,
            strerror(errno)
        );
        return QN_ERR_IO;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5117",
            0,
            0,
            "cannot seek trust store '%s'",
            path
        );
        fclose(file);
        return QN_ERR_IO;
    }

    long end = ftell(file);

    if (end < 0) {
        qn_diag_set_code(
            diag,
            "QN-E5117",
            0,
            0,
            "cannot determine trust store size for '%s'",
            path
        );
        fclose(file);
        return QN_ERR_IO;
    }

    if ((uint64_t)end >
        QN_TRUST_STORE_MAX_FILE_BYTES) {
        qn_diag_set_code(
            diag,
            "QN-E5111",
            0,
            0,
            "trust store exceeds %u bytes",
            QN_TRUST_STORE_MAX_FILE_BYTES
        );
        fclose(file);
        return QN_ERR_LIMIT;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5117",
            0,
            0,
            "cannot rewind trust store '%s'",
            path
        );
        fclose(file);
        return QN_ERR_IO;
    }

    uint8_t data[QN_TRUST_STORE_MAX_FILE_BYTES];
    size_t size = (size_t)end;

    if (size > 0u &&
        fread(data, 1u, size, file) != size) {
        qn_diag_set_code(
            diag,
            "QN-E5117",
            0,
            0,
            "short read from trust store '%s'",
            path
        );
        fclose(file);
        return QN_ERR_IO;
    }

    if (fclose(file) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5117",
            0,
            0,
            "cannot close trust store '%s'",
            path
        );
        return QN_ERR_IO;
    }

    return qn_trust_store_parse_text(
        data,
        size,
        out,
        diag
    );
}
