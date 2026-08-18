#include "qn.h"
#include "qn_qbc_v10_data.h"
#include "qn_v10_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int qn_legacy_main(int argc, char **argv);

static int v10_print_diag(QNStatus status, const QNDiagnostic *diag) {
    const char *code = diag && diag->code[0] ? diag->code : "QN-E-GENERIC";
    if (diag && diag->line > 0) {
        fprintf(stderr, "error[%s][%d:%d]: %s\n",
                code, diag->line, diag->column, diag->message);
    } else {
        fprintf(stderr, "error[%s]: %s\n",
                code, diag ? diag->message : "V10 command failed");
    }
    return (int)status;
}

static bool ident_start(unsigned char c) {
    return isalpha(c) || c == '_';
}

static bool ident_continue(unsigned char c) {
    return isalnum(c) || c == '_';
}

static bool word_equal_ci(const char *text, size_t length, const char *word) {
    size_t word_length = strlen(word);
    if (length != word_length) return false;
    for (size_t i = 0u; i < length; ++i) {
        if (tolower((unsigned char)text[i]) !=
            tolower((unsigned char)word[i])) {
            return false;
        }
    }
    return true;
}

static void skip_horizontal(const char *source, size_t *at) {
    while (source[*at] == ' ' || source[*at] == '\t' || source[*at] == '\r') {
        ++*at;
    }
}

static bool source_claims_v10_data(const char *source) {
    if (!source) return false;

    size_t i = 0u;
    while (source[i]) {
        while (source[i] == ' ' || source[i] == '\t' ||
               source[i] == '\r' || source[i] == '\n' || source[i] == ';') {
            ++i;
        }
        if (!source[i]) break;

        if (source[i] == '#') {
            while (source[i] && source[i] != '\n') ++i;
            continue;
        }
        if (source[i] == '/' && source[i + 1u] == '/') {
            i += 2u;
            while (source[i] && source[i] != '\n') ++i;
            continue;
        }

        size_t j = i;
        if (ident_start((unsigned char)source[j])) {
            size_t keyword_start = j++;
            while (ident_continue((unsigned char)source[j])) ++j;
            if (word_equal_ci(source + keyword_start, j - keyword_start, "let")) {
                skip_horizontal(source, &j);
                if (ident_start((unsigned char)source[j])) {
                    ++j;
                    while (ident_continue((unsigned char)source[j])) ++j;
                    skip_horizontal(source, &j);
                    if (source[j] == ':') {
                        ++j;
                        skip_horizontal(source, &j);
                        size_t type_start = j;
                        if (ident_start((unsigned char)source[j])) {
                            ++j;
                            while (ident_continue((unsigned char)source[j])) ++j;
                            size_t type_length = j - type_start;
                            if (word_equal_ci(source + type_start, type_length, "f32") ||
                                word_equal_ci(source + type_start, type_length, "string") ||
                                word_equal_ci(source + type_start, type_length, "bytes")) {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        bool quoted = false;
        bool escaped = false;
        while (source[i]) {
            char c = source[i];
            if (quoted) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    quoted = false;
                }
                ++i;
                continue;
            }
            if (c == '"') {
                quoted = true;
                ++i;
                continue;
            }
            if (c == '#' || (c == '/' && source[i + 1u] == '/')) {
                while (source[i] && source[i] != '\n') ++i;
                break;
            }
            if (c == '\n' || c == ';') {
                ++i;
                break;
            }
            ++i;
        }
    }
    return false;
}

static QNStatus build_v10_qir(const char *source,
                              QNV10DataProgram *program,
                              QNV10DataQIRProgram *qir,
                              QNDiagnostic *diag) {
    QNStatus status = qn_v10_data_parse_source(source, program, diag);
    if (status != QN_OK) return status;

    status = qn_v10_data_qir_build(program, qir, diag);
    if (status != QN_OK) {
        qn_v10_data_qir_free(qir);
        qn_v10_data_program_free(program);
    }
    return status;
}

static int command_v10_check(const char *source, QNDiagnostic *diag) {
    QNV10DataProgram program = {0};
    QNV10DataQIRProgram qir = {0};
    QNStatus status = build_v10_qir(source, &program, &qir, diag);
    if (status != QN_OK) return v10_print_diag(status, diag);

    printf("PASS: QBIT_NOVA_NATIVE_DATA_CHECK_V10_STEP2C\n");
    printf("values=%u\n", (unsigned)qir.value_count);
    printf("constant_pool_bytes=%u\n", (unsigned)qir.constant_bytes_size);
    printf("qbc_version_required=10\n");

    qn_v10_data_qir_free(&qir);
    qn_v10_data_program_free(&program);
    return 0;
}

static int command_v10_qir(const char *source, QNDiagnostic *diag) {
    QNV10DataProgram program = {0};
    QNV10DataQIRProgram qir = {0};
    QNStatus status = build_v10_qir(source, &program, &qir, diag);
    if (status != QN_OK) return v10_print_diag(status, diag);

    printf("QBIT_NOVA_V10_DATA_QIR_STEP2C\n");
    printf("abi=%u\n", (unsigned)qir.abi_version);
    printf("values=%u\n", (unsigned)qir.value_count);
    printf("constant_pool_bytes=%u\n", (unsigned)qir.constant_bytes_size);
    for (uint16_t i = 0u; i < qir.value_count; ++i) {
        const QNV10DataQIRValue *value = &qir.values[i];
        printf("value[%u].name=%s\n", (unsigned)i, value->name);
        printf("value[%u].type=%s\n", (unsigned)i,
               qn_value_kind_name(value->kind));
        if (value->kind == QN_VALUE_F32) {
            printf("value[%u].f32_bits=0x%08x\n",
                   (unsigned)i, (unsigned)value->f32_bits);
        } else {
            printf("value[%u].offset=%u\n",
                   (unsigned)i, (unsigned)value->constant_offset);
            printf("value[%u].bytes=%u\n",
                   (unsigned)i, (unsigned)value->byte_length);
        }
    }
    printf("requires_qbc_v10=true\n");

    qn_v10_data_qir_free(&qir);
    qn_v10_data_program_free(&program);
    return 0;
}

static int command_v10_build(int argc,
                             char **argv,
                             const char *source,
                             size_t source_size,
                             QNDiagnostic *diag) {
    const char *out_path = NULL;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            if (out_path) {
                qn_diag_set_code(diag, "QN-E7830", 0, 0,
                                 "duplicate -o for V10 build");
                return v10_print_diag(QN_ERR_PARSE, diag);
            }
            out_path = argv[++i];
        } else {
            qn_diag_set_code(diag, "QN-E7830", 0, 0,
                             "unknown V10 build option '%s'", argv[i]);
            return v10_print_diag(QN_ERR_PARSE, diag);
        }
    }
    if (!out_path) {
        qn_diag_set_code(diag, "QN-E7830", 0, 0,
                         "V10 build requires -o <file.qbc>");
        return v10_print_diag(QN_ERR_PARSE, diag);
    }

    QNV10DataProgram program = {0};
    QNV10DataQIRProgram qir = {0};
    uint8_t *data = NULL;
    size_t data_size = 0u;
    uint8_t source_digest[32];
    qn_sha256((const uint8_t *)source, source_size, source_digest);

    QNStatus status = build_v10_qir(source, &program, &qir, diag);
    if (status == QN_OK) {
        status = qn_qbc_v10_data_encode(&qir, source_digest,
                                        &data, &data_size, diag);
    }
    if (status == QN_OK &&
        !qn_write_binary_file_atomic(out_path, data, data_size, diag)) {
        status = QN_ERR_IO;
    }

    if (status == QN_OK) {
        uint8_t digest[32];
        char hex[65];
        qn_sha256(data, data_size, digest);
        qn_hex32(digest, hex);
        printf("QBIT_NOVA_QBC_V10_DATA_BUILD_STEP2C\n");
        printf("output=%s\n", out_path);
        printf("qbc_version=10\n");
        printf("values=%u\n", (unsigned)qir.value_count);
        printf("constant_pool_bytes=%u\n", (unsigned)qir.constant_bytes_size);
        printf("bytes=%zu\n", data_size);
        printf("sha256=%s\n", hex);
        printf("qvm_execution=false\n");
    }

    free(data);
    qn_v10_data_qir_free(&qir);
    qn_v10_data_program_free(&program);
    return status == QN_OK ? 0 : v10_print_diag(status, diag);
}

static bool file_is_qbc_v10(const char *path) {
    if (!path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[8] = {0};
    size_t n = fread(header, 1u, sizeof(header), file);
    fclose(file);
    if (n != sizeof(header) || memcmp(header, "QBCN", 4u) != 0) return false;
    uint16_t version = (uint16_t)header[4] | ((uint16_t)header[5] << 8);
    return version == QN_QBC_V10_VERSION;
}

int main(int argc, char **argv) {
    if (argc >= 3 && argv && argv[1] && argv[2]) {
        if (strcmp(argv[1], "exec") == 0 && file_is_qbc_v10(argv[2])) {
            QNDiagnostic diag = {0};
            qn_diag_set_code(&diag, "QN-E7832", 0, 0,
                             "QBC v10 native-data execution is not implemented; build/check/qir are enabled only");
            return v10_print_diag(QN_ERR_RUNTIME, &diag);
        }

        if (strcmp(argv[1], "build") == 0 ||
            strcmp(argv[1], "check") == 0 ||
            strcmp(argv[1], "qir") == 0 ||
            strcmp(argv[1], "run") == 0) {
            QNDiagnostic diag = {0};
            size_t source_size = 0u;
            char *source = qn_read_text_file(argv[2], &source_size, &diag);
            if (source) {
                bool claims_v10 = source_claims_v10_data(source);
                if (claims_v10) {
                    int result;
                    if (strcmp(argv[1], "check") == 0) {
                        result = command_v10_check(source, &diag);
                    } else if (strcmp(argv[1], "qir") == 0) {
                        result = command_v10_qir(source, &diag);
                    } else if (strcmp(argv[1], "build") == 0) {
                        result = command_v10_build(argc, argv, source,
                                                   source_size, &diag);
                    } else {
                        qn_diag_set_code(&diag, "QN-E7831", 0, 0,
                                         "V10 native-data source is compiler-enabled but QVM execution is not implemented");
                        result = v10_print_diag(QN_ERR_RUNTIME, &diag);
                    }
                    free(source);
                    return result;
                }
                free(source);
            }
        }
    }

    return qn_legacy_main(argc, argv);
}
