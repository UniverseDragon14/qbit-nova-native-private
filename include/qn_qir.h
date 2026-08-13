#ifndef QN_QIR_H
#define QN_QIR_H

#include "qn_ast.h"
#include "qn_qbc.h"

typedef enum {
    QIR_TYPE_NONE = 0,
    QIR_TYPE_QUBIT,
    QIR_TYPE_QREG,
    QIR_TYPE_RESULT,
    QIR_TYPE_U32_VECTOR,
    QIR_TYPE_U32,
    QIR_TYPE_BOOL
} QNQIRType;

typedef enum {
    QIR_OP_H = 1,
    QIR_OP_X,
    QIR_OP_Z,
    QIR_OP_CX,
    QIR_OP_MEASURE_ALL,
    QIR_OP_EMIT,
    QIR_OP_U32_VECTOR_ADD,
    QIR_OP_U32_CONST,
    QIR_OP_U32_ADD,
    QIR_OP_U32_SUB,
    QIR_OP_U32_MUL,
    QIR_OP_U32_DIV,
    QIR_OP_U32_EMIT,
    QIR_OP_U32_EQ,
    QIR_OP_U32_NE,
    QIR_OP_U32_LT,
    QIR_OP_U32_LE,
    QIR_OP_U32_GT,
    QIR_OP_U32_GE,
    QIR_OP_BOOL_EMIT,
    QIR_OP_JUMP_IF_FALSE,
    QIR_OP_JUMP,
    QIR_OP_U32_SET_ADD,
    QIR_OP_U32_SET_SUB,
    QIR_OP_U32_SET_MUL,
    QIR_OP_U32_SET_DIV,
    QIR_OP_REPEAT_ENTER,
    QIR_OP_REPEAT_NEXT,
    QIR_OP_CALL,
    QIR_OP_RETURN
} QNQIROpcode;

typedef struct {
    QNQIRType type;
    uint16_t register_id;
    uint16_t qubit_index;
    char name[QN_NAME_CAP];
} QNQIRValue;

typedef struct {
    char name[QN_NAME_CAP];
    QNQIRType type;
} QNScalarInfo;

typedef struct {
    QNQIROpcode opcode;
    int line;
    int column;
    QNQIRValue a;
    QNQIRValue b;
    QNQIRValue out;
    uint32_t imm;
} QNQIRInstruction;


typedef struct {
    char name[QN_NAME_CAP];
    uint32_t entry_instruction;
    uint32_t end_instruction;
    uint16_t scalar_count;
    uint8_t param_count;
} QNQIRFunctionInfo;

typedef struct {
    uint16_t total_qubits;
    uint16_t register_count;
    uint16_t scalar_count;
    uint64_t scalar_bool_mask;
    uint16_t function_count;
    uint32_t main_entry_instruction;
    QNQIRFunctionInfo functions[QN_MAX_FUNCTIONS];
    uint64_t initial_basis;
    uint32_t default_shots;
    uint64_t default_seed;
    QNCapabilityMask capability_mask;
    uint8_t source_digest[32];
    QNRegisterInfo registers[QN_MAX_REGISTERS];
    QNScalarInfo scalars[QN_MAX_SCALARS];
    QNQIRInstruction *instructions;
    size_t instruction_count;
} QNQIRProgram;

void qn_qir_free(QNQIRProgram *qir);
const char *qn_qir_type_name(QNQIRType type);
const char *qn_qir_opcode_name(QNQIROpcode opcode);

QNStatus qn_qir_build(const QNProgram *program,
                      const uint8_t source_digest[32],
                      QNQIRProgram *out,
                      QNDiagnostic *diag);

QNStatus qn_qir_lower(const QNQIRProgram *qir,
                      QNBytecode *out,
                      QNDiagnostic *diag);

void qn_qir_dump(const QNQIRProgram *qir, FILE *stream);

#endif
