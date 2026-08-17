#ifndef QN_V10_DATA_H
#define QN_V10_DATA_H

#include "qn_media.h"

enum {
    QN_V10_DATA_ABI_V1 = 1u,
    QN_V10_MAX_DECLS = QN_MAX_SCALARS,
    QN_V10_MAX_CONSTANT_POOL_BYTES = QN_MAX_BYTES_BUFFER
};

typedef struct {
    char name[QN_NAME_CAP];
    QNValueKind kind;
    int line;
    int column;
    union {
        float f32;
        struct {
            uint8_t *data;
            uint32_t byte_length;
        } blob;
    } as;
} QNV10DataDecl;

typedef struct {
    QNV10DataDecl declarations[QN_V10_MAX_DECLS];
    uint16_t count;
} QNV10DataProgram;

typedef struct {
    char name[QN_NAME_CAP];
    QNValueKind kind;
    uint32_t constant_offset;
    uint32_t byte_length;
    uint32_t f32_bits;
} QNV10DataQIRValue;

typedef struct {
    uint16_t abi_version;
    uint16_t value_count;
    QNV10DataQIRValue values[QN_V10_MAX_DECLS];
    uint8_t *constant_bytes;
    uint32_t constant_bytes_size;
    bool requires_qbc_v10;
} QNV10DataQIRProgram;

QNStatus qn_v10_data_parse_source(const char *source,
                                  QNV10DataProgram *out,
                                  QNDiagnostic *diag);
void qn_v10_data_program_free(QNV10DataProgram *program);
QNStatus qn_v10_data_qir_build(const QNV10DataProgram *program,
                               QNV10DataQIRProgram *out,
                               QNDiagnostic *diag);
void qn_v10_data_qir_free(QNV10DataQIRProgram *qir);
QNStatus qn_v10_data_qbc_guard(const QNV10DataQIRProgram *qir,
                               QNDiagnostic *diag);

#endif
