#ifndef QN_QBC_H
#define QN_QBC_H

#include "qn_ast.h"
#include "qn_guard.h"

typedef enum {
    OP_H = 0x20,
    OP_X = 0x21,
    OP_Z = 0x22,
    OP_CX = 0x23,
    OP_MEASURE_ALL = 0x30,
    OP_EMIT = 0x40,
    OP_U32_VECTOR_ADD = 0x50,
    OP_U32_CONST = 0x51,
    OP_U32_ADD = 0x52,
    OP_U32_EMIT = 0x53,
    OP_U32_SUB = 0x54,
    OP_U32_MUL = 0x55,
    OP_U32_DIV = 0x56,
    OP_U32_EQ = 0x57,
    OP_U32_NE = 0x58,
    OP_U32_LT = 0x59,
    OP_U32_LE = 0x5a,
    OP_U32_GT = 0x5b,
    OP_U32_GE = 0x5c,
    OP_BOOL_EMIT = 0x5d,
    OP_END = 0x7f
} QNOpcode;

typedef struct {
    uint8_t opcode;
    uint8_t a;
    uint8_t b;
    uint8_t flags;
    uint32_t imm;
} QNInstruction;

typedef struct {
    char name[QN_NAME_CAP];
    uint16_t base;
    uint16_t width;
} QNRegisterInfo;

typedef struct {
    uint16_t total_qubits;
    uint16_t register_count;
    uint16_t scalar_count;
    uint64_t scalar_bool_mask;
    uint64_t initial_basis;
    uint32_t default_shots;
    uint64_t default_seed;
    QNCapabilityMask capability_mask;
    uint8_t source_digest[32];
    QNRegisterInfo registers[QN_MAX_REGISTERS];
    QNInstruction *instructions;
    size_t instruction_count;
} QNBytecode;

void qn_bytecode_free(QNBytecode *bc);
QNStatus qn_compile(const QNProgram *program, const uint8_t source_digest[32],
                    QNBytecode *out, QNDiagnostic *diag);
QNStatus qn_qbc_encode(const QNBytecode *bc, uint8_t **data_out, size_t *size_out,
                       QNDiagnostic *diag);
QNStatus qn_qbc_decode(const uint8_t *data, size_t size, QNBytecode *out,
                       QNDiagnostic *diag);
bool qn_qbc_is_bounded_u32_vector_add(const QNBytecode *bc);
bool qn_qbc_is_u32_scalar_program(const QNBytecode *bc);
bool qn_qbc_is_typed_scalar_program(const QNBytecode *bc);

#endif
