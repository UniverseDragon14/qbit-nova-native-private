#ifndef QN_H
#define QN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define QN_VERSION_MAJOR 0
#define QN_VERSION_MINOR 5
#define QN_VERSION_PATCH 0

#define QN_MAX_SOURCE_BYTES (1024u * 1024u)
#define QN_MAX_TOKENS 65536u
#define QN_MAX_STATEMENTS 16384u
#define QN_MAX_REGISTERS 32u
#define QN_MAX_QUBITS 20u
#define QN_MAX_INSTRUCTIONS 65536u
#define QN_MAX_SHOTS 1000000u
#define QN_U32_VECTOR_ADD_COUNT 256u
#define QN_MAX_SCALARS 64u
#define QN_MAX_U32_SCALARS QN_MAX_SCALARS
#define QN_MAX_REPEAT_ITERATIONS 1024u
#define QN_MAX_EXECUTION_STEPS 1000000u
#define QN_MAX_FUNCTIONS 16u
#define QN_MAX_FUNCTION_PARAMS 2u
#define QN_MAX_CALL_DEPTH 8u
#define QN_MAX_RUNTIME_INPUTS 16u

/* Stage7 Step9 native tensor limits. */
#define QN_MAX_TENSORS 16u
#define QN_MAX_TENSOR_ELEMENTS 65536u
#define QN_MAX_TENSOR_BYTES (16u * 1024u * 1024u)
#define QN_MAX_TOTAL_TENSOR_BYTES (64u * 1024u * 1024u)
#define QN_NAME_CAP 64u
#define QN_PATH_CAP 4096u

typedef enum {
    QN_OK = 0,
    QN_ERR_IO = 2,
    QN_ERR_LEX = 3,
    QN_ERR_PARSE = 4,
    QN_ERR_SEMANTIC = 5,
    QN_ERR_QBC = 6,
    QN_ERR_RUNTIME = 7,
    QN_ERR_LIMIT = 8
} QNStatus;

typedef struct {
    int line;
    int column;
    char code[32];
    char message[256];
} QNDiagnostic;

void qn_diag_set(QNDiagnostic *diag, int line, int column, const char *fmt, ...);
void qn_diag_set_code(QNDiagnostic *diag, const char *code,
                      int line, int column, const char *fmt, ...);
char *qn_read_text_file(const char *path, size_t *size_out, QNDiagnostic *diag);
bool qn_write_binary_file(const char *path, const uint8_t *data, size_t size, QNDiagnostic *diag);
bool qn_write_binary_file_atomic(const char *path, const uint8_t *data, size_t size, QNDiagnostic *diag);
uint8_t *qn_read_binary_file(const char *path, size_t *size_out, QNDiagnostic *diag);
void qn_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void qn_hex32(const uint8_t digest[32], char out[65]);

#endif
