#ifndef QN_QIR_H
#define QN_QIR_H

#include "qn_ast.h"
#include "qn_qbc.h"

typedef enum {
    QIR_TYPE_NONE = 0,
    QIR_TYPE_QUBIT,
    QIR_TYPE_QREG,
    QIR_TYPE_RESULT
} QNQIRType;

typedef enum {
    QIR_OP_H = 1,
    QIR_OP_X,
    QIR_OP_Z,
    QIR_OP_CX,
    QIR_OP_MEASURE_ALL,
    QIR_OP_EMIT
} QNQIROpcode;

typedef struct {
    QNQIRType type;
    uint16_t register_id;
    uint16_t qubit_index;
    char name[QN_NAME_CAP];
} QNQIRValue;

typedef struct {
    QNQIROpcode opcode;
    int line;
    int column;
    QNQIRValue a;
    QNQIRValue b;
    QNQIRValue out;
} QNQIRInstruction;

typedef struct {
    uint16_t total_qubits;
    uint16_t register_count;
    uint64_t initial_basis;
    uint32_t default_shots;
    uint64_t default_seed;
    QNCapabilityMask capability_mask;
    uint8_t source_digest[32];
    QNRegisterInfo registers[QN_MAX_REGISTERS];
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
