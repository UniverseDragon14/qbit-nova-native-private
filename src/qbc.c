#include "qn_qbc.h"
#include "qn_qir.h"

#include <stdlib.h>
#include <string.h>

#define QBC_V1_HEADER_SIZE 64u
#define QBC_V2_HEADER_SIZE 72u
#define QBC_V3_HEADER_SIZE 80u
#define QBC_REG_SIZE 68u
#define QBC_INSN_SIZE 8u

void qn_bytecode_free(QNBytecode *bc) {
    if (!bc) return;
    free(bc->instructions);
    memset(bc, 0, sizeof(*bc));
}

QNStatus qn_compile(const QNProgram *program,
                    const uint8_t source_digest[32],
                    QNBytecode *out,
                    QNDiagnostic *diag) {
    QNQIRProgram qir;
    memset(&qir, 0, sizeof(qir));

    QNStatus status = qn_qir_build(
        program, source_digest, &qir, diag
    );
    if (status != QN_OK) return status;

    status = qn_qir_lower(&qir, out, diag);
    qn_qir_free(&qir);
    return status;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

QNStatus qn_qbc_encode(const QNBytecode *bc,
                       uint8_t **data_out,
                       size_t *size_out,
                       QNDiagnostic *diag) {
    size_t size =
        QBC_V3_HEADER_SIZE +
        bc->register_count * QBC_REG_SIZE +
        bc->instruction_count * QBC_INSN_SIZE;

    uint8_t *data = calloc(size, 1);
    if (!data) {
        qn_diag_set(diag, 0, 0, "out of memory encoding QBC");
        return QN_ERR_QBC;
    }

    memcpy(data, "QBCN", 4);
    put16(data + 4, 3u);
    put16(data + 6, QBC_V3_HEADER_SIZE);
    put32(data + 8, (uint32_t)bc->instruction_count);
    put16(data + 12, bc->total_qubits);
    put16(data + 14, bc->register_count);
    put64(data + 16, bc->initial_basis);
    put32(data + 24, bc->default_shots);
    put64(data + 28, bc->default_seed);
    memcpy(data + 36, bc->source_digest, 32);
    put64(data + 68, bc->capability_mask);

    size_t at = QBC_V3_HEADER_SIZE;

    for (uint16_t i = 0; i < bc->register_count; ++i) {
        memcpy(data + at, bc->registers[i].name, QN_NAME_CAP);
        put16(data + at + 64, bc->registers[i].base);
        put16(data + at + 66, bc->registers[i].width);
        at += QBC_REG_SIZE;
    }

    for (size_t i = 0; i < bc->instruction_count; ++i) {
        const QNInstruction *ins = &bc->instructions[i];
        data[at] = ins->opcode;
        data[at + 1] = ins->a;
        data[at + 2] = ins->b;
        data[at + 3] = ins->flags;
        put32(data + at + 4, ins->imm);
        at += QBC_INSN_SIZE;
    }

    *data_out = data;
    *size_out = size;
    return QN_OK;
}

QNStatus qn_qbc_decode(const uint8_t *data,
                       size_t size,
                       QNBytecode *out,
                       QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));

    if (size < QBC_V1_HEADER_SIZE ||
        memcmp(data, "QBCN", 4) != 0) {
        qn_diag_set(diag, 0, 0, "invalid QBC magic");
        return QN_ERR_QBC;
    }

    uint16_t version = get16(data + 4);
    uint16_t header_size = get16(data + 6);

    if (!((version == 1u && header_size == QBC_V1_HEADER_SIZE) ||
          (version == 2u && header_size == QBC_V2_HEADER_SIZE) ||
          (version == 3u && header_size == QBC_V3_HEADER_SIZE))) {
        qn_diag_set(diag, 0, 0,
                    "unsupported QBC version/header");
        return QN_ERR_QBC;
    }

    if (size < header_size) {
        qn_diag_set(diag, 0, 0, "truncated QBC header");
        return QN_ERR_QBC;
    }

    uint32_t instruction_count = get32(data + 8);
    uint16_t qubits = get16(data + 12);
    uint16_t registers = get16(data + 14);

    if (qubits == 0u ||
        qubits > QN_MAX_QUBITS ||
        registers > QN_MAX_REGISTERS ||
        instruction_count > QN_MAX_INSTRUCTIONS) {
        qn_diag_set(diag, 0, 0, "QBC limits invalid");
        return QN_ERR_QBC;
    }

    size_t needed =
        header_size +
        (size_t)registers * QBC_REG_SIZE +
        (size_t)instruction_count * QBC_INSN_SIZE;

    if (needed != size) {
        qn_diag_set(diag, 0, 0, "QBC size mismatch");
        return QN_ERR_QBC;
    }

    out->total_qubits = qubits;
    out->register_count = registers;
    out->initial_basis = get64(data + 16);
    out->default_shots = get32(data + 24);
    out->default_seed = get64(data + 28);

    if (version == 1u) {
        memcpy(out->source_digest, data + 36, 28);
    } else {
        memcpy(out->source_digest, data + 36, 32);
    }

    if (version == 3u) {
        out->capability_mask = get64(data + 68);
    }

    size_t at = header_size;

    for (uint16_t i = 0; i < registers; ++i) {
        memcpy(out->registers[i].name,
               data + at,
               QN_NAME_CAP);
        out->registers[i].name[QN_NAME_CAP - 1] = '\0';
        out->registers[i].base = get16(data + at + 64);
        out->registers[i].width = get16(data + at + 66);

        if ((uint32_t)out->registers[i].base +
            out->registers[i].width > qubits) {
            qn_diag_set(diag, 0, 0,
                        "QBC register range out of bounds");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        at += QBC_REG_SIZE;
    }

    out->instructions = calloc(
        instruction_count,
        sizeof(*out->instructions)
    );
    if (!out->instructions) {
        qn_diag_set(diag, 0, 0, "out of memory decoding QBC");
        return QN_ERR_QBC;
    }

    out->instruction_count = instruction_count;

    for (uint32_t i = 0; i < instruction_count; ++i) {
        QNInstruction *ins = &out->instructions[i];
        ins->opcode = data[at];
        ins->a = data[at + 1];
        ins->b = data[at + 2];
        ins->flags = data[at + 3];
        ins->imm = get32(data + at + 4);

        if ((ins->opcode == OP_H ||
             ins->opcode == OP_X ||
             ins->opcode == OP_Z ||
             ins->opcode == OP_CX) &&
            ins->a >= qubits) {
            qn_diag_set(diag, 0, 0,
                        "QBC qubit index out of bounds");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if (ins->opcode == OP_CX && ins->b >= qubits) {
            qn_diag_set(diag, 0, 0,
                        "QBC cx target out of bounds");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        at += QBC_INSN_SIZE;
    }

    if (version < 3u) {
        for (size_t i = 0; i < out->instruction_count; ++i) {
            switch (out->instructions[i].opcode) {
                case OP_H:
                case OP_X:
                case OP_Z:
                case OP_CX:
                case OP_MEASURE_ALL:
                    out->capability_mask |= QN_CAP_QUANTUM_SIMULATE;
                    break;
                case OP_EMIT:
                    out->capability_mask |= QN_CAP_EVIDENCE_EMIT;
                    break;
                default:
                    break;
            }
        }
    }

    if ((out->capability_mask & ~QN_CAP_KNOWN) != 0u) {
        qn_diag_set_code(
            diag,
            "QN-E-QBC-CAP-001",
            0,
            0,
            "QBC declares unknown capability bits"
        );
        qn_bytecode_free(out);
        return QN_ERR_QBC;
    }

    return QN_OK;
}
