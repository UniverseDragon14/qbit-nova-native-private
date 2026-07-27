#include "qn_qir.h"

#include <stdlib.h>
#include <string.h>

static int find_reg(const QNQIRProgram *qir, const char *name) {
    for (uint16_t i = 0; i < qir->register_count; ++i) {
        if (strcmp(qir->registers[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool append_qir(QNQIRProgram *qir,
                       QNQIRInstruction instruction,
                       QNDiagnostic *diag) {
    if (qir->instruction_count >= QN_MAX_INSTRUCTIONS) {
        qn_diag_set(diag, instruction.line, instruction.column,
                    "typed QIR instruction limit exceeded");
        return false;
    }

    QNQIRInstruction *next = realloc(
        qir->instructions,
        (qir->instruction_count + 1) * sizeof(*next)
    );
    if (!next) {
        qn_diag_set(diag, instruction.line, instruction.column,
                    "out of memory building typed QIR");
        return false;
    }

    qir->instructions = next;
    qir->instructions[qir->instruction_count++] = instruction;
    return true;
}

static bool resolve_target(const QNQIRProgram *qir,
                           const QNTarget *target,
                           QNQIRValue *out,
                           QNDiagnostic *diag) {
    int reg_id = find_reg(qir, target->reg);
    if (reg_id < 0) {
        qn_diag_set(diag, target->line, target->column,
                    "unknown register '%s'", target->reg);
        return false;
    }

    const QNRegisterInfo *reg = &qir->registers[reg_id];
    uint32_t local = target->has_index ? target->index : 0u;

    if (!target->has_index && reg->width != 1u) {
        qn_diag_set(diag, target->line, target->column,
                    "register '%s' has %u qubits; index required",
                    target->reg, reg->width);
        return false;
    }

    if (local >= reg->width) {
        qn_diag_set(diag, target->line, target->column,
                    "index %u outside register '%s[%u]'",
                    local, target->reg, reg->width);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->type = QIR_TYPE_QUBIT;
    out->register_id = (uint16_t)reg_id;
    out->qubit_index = (uint16_t)local;
    snprintf(out->name, sizeof(out->name), "%s", target->reg);
    return true;
}

static QNQIRValue register_value(const QNQIRProgram *qir, uint16_t reg_id) {
    QNQIRValue value;
    memset(&value, 0, sizeof(value));
    value.type = QIR_TYPE_QREG;
    value.register_id = reg_id;
    snprintf(value.name, sizeof(value.name), "%s", qir->registers[reg_id].name);
    return value;
}

static QNQIRValue result_value(const char *name) {
    QNQIRValue value;
    memset(&value, 0, sizeof(value));
    value.type = QIR_TYPE_RESULT;
    snprintf(value.name, sizeof(value.name), "%s", name);
    return value;
}

void qn_qir_free(QNQIRProgram *qir) {
    if (!qir) return;
    free(qir->instructions);
    memset(qir, 0, sizeof(*qir));
}

const char *qn_qir_type_name(QNQIRType type) {
    switch (type) {
        case QIR_TYPE_NONE: return "none";
        case QIR_TYPE_QUBIT: return "qbit";
        case QIR_TYPE_QREG: return "qreg";
        case QIR_TYPE_RESULT: return "result";
        default: return "invalid";
    }
}

const char *qn_qir_opcode_name(QNQIROpcode opcode) {
    switch (opcode) {
        case QIR_OP_H: return "H";
        case QIR_OP_X: return "X";
        case QIR_OP_Z: return "Z";
        case QIR_OP_CX: return "CX";
        case QIR_OP_MEASURE_ALL: return "MEASURE.ALL";
        case QIR_OP_EMIT: return "EMIT";
        default: return "INVALID";
    }
}

QNStatus qn_qir_build(const QNProgram *program,
                      const uint8_t source_digest[32],
                      QNQIRProgram *out,
                      QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));
    memcpy(out->source_digest, source_digest, 32);
    out->default_shots = 1u;
    out->default_seed = 1u;

    for (size_t i = 0; i < program->count; ++i) {
        const QNStmt *stmt = &program->items[i];
        if (stmt->kind != STMT_QREG) continue;

        if (out->register_count >= QN_MAX_REGISTERS) {
            qn_diag_set(diag, stmt->line, stmt->column,
                        "register limit exceeded");
            goto fail;
        }

        if (find_reg(out, stmt->as.qreg.name) >= 0) {
            qn_diag_set(diag, stmt->line, stmt->column,
                        "duplicate register '%s'", stmt->as.qreg.name);
            goto fail;
        }

        if ((uint32_t)out->total_qubits + stmt->as.qreg.width >
            QN_MAX_QUBITS) {
            qn_diag_set(diag, stmt->line, stmt->column,
                        "total qubits exceed %u", QN_MAX_QUBITS);
            goto fail;
        }

        QNRegisterInfo *reg = &out->registers[out->register_count++];
        snprintf(reg->name, sizeof(reg->name), "%s", stmt->as.qreg.name);
        reg->base = out->total_qubits;
        reg->width = (uint16_t)stmt->as.qreg.width;

        out->initial_basis |= stmt->as.qreg.initial_basis << reg->base;
        out->total_qubits =
            (uint16_t)(out->total_qubits + reg->width);
    }

    if (out->total_qubits == 0u) {
        qn_diag_set(diag, 1, 1, "program declares no qbit/qreg");
        goto fail;
    }

    bool measured = false;
    char measured_name[QN_NAME_CAP] = {0};

    for (size_t i = 0; i < program->count; ++i) {
        const QNStmt *stmt = &program->items[i];
        QNQIRInstruction ins;
        memset(&ins, 0, sizeof(ins));
        ins.line = stmt->line;
        ins.column = stmt->column;

        switch (stmt->kind) {
            case STMT_QREG:
                break;

            case STMT_H:
            case STMT_X:
            case STMT_Z:
                if (measured) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "gate after measurement is not allowed");
                    goto fail;
                }
                ins.opcode =
                    stmt->kind == STMT_H ? QIR_OP_H :
                    stmt->kind == STMT_X ? QIR_OP_X : QIR_OP_Z;
                if (!resolve_target(out, &stmt->as.unary.target,
                                    &ins.a, diag)) {
                    goto fail;
                }
                out->capability_mask |= QN_CAP_QUANTUM_SIMULATE;
                if (!append_qir(out, ins, diag)) goto fail;
                break;

            case STMT_CX:
                if (measured) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "gate after measurement is not allowed");
                    goto fail;
                }
                ins.opcode = QIR_OP_CX;
                if (!resolve_target(out, &stmt->as.cx.control,
                                    &ins.a, diag) ||
                    !resolve_target(out, &stmt->as.cx.target,
                                    &ins.b, diag)) {
                    goto fail;
                }
                if (ins.a.register_id == ins.b.register_id &&
                    ins.a.qubit_index == ins.b.qubit_index) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "cx control and target must differ");
                    goto fail;
                }
                out->capability_mask |= QN_CAP_QUANTUM_SIMULATE;
                if (!append_qir(out, ins, diag)) goto fail;
                break;

            case STMT_GHZ: {
                if (measured) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "ghz after measurement is not allowed");
                    goto fail;
                }

                int reg_id = find_reg(out, stmt->as.ghz.reg);
                if (reg_id < 0) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "unknown register '%s'",
                                stmt->as.ghz.reg);
                    goto fail;
                }

                const QNRegisterInfo *reg = &out->registers[reg_id];
                if (reg->width < 2u) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "ghz requires at least 2 qubits");
                    goto fail;
                }

                out->capability_mask |= QN_CAP_QUANTUM_SIMULATE;
                ins.opcode = QIR_OP_H;
                ins.a = (QNQIRValue){
                    .type = QIR_TYPE_QUBIT,
                    .register_id = (uint16_t)reg_id,
                    .qubit_index = 0u
                };
                snprintf(ins.a.name, sizeof(ins.a.name), "%s", reg->name);
                if (!append_qir(out, ins, diag)) goto fail;

                for (uint16_t q = 1u; q < reg->width; ++q) {
                    memset(&ins, 0, sizeof(ins));
                    ins.opcode = QIR_OP_CX;
                    ins.line = stmt->line;
                    ins.column = stmt->column;
                    ins.a = (QNQIRValue){
                        .type = QIR_TYPE_QUBIT,
                        .register_id = (uint16_t)reg_id,
                        .qubit_index = 0u
                    };
                    ins.b = (QNQIRValue){
                        .type = QIR_TYPE_QUBIT,
                        .register_id = (uint16_t)reg_id,
                        .qubit_index = q
                    };
                    snprintf(ins.a.name, sizeof(ins.a.name), "%s", reg->name);
                    snprintf(ins.b.name, sizeof(ins.b.name), "%s", reg->name);
                    if (!append_qir(out, ins, diag)) goto fail;
                }
                break;
            }

            case STMT_MEASURE: {
                if (measured) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "only one measurement block is allowed in v0.2");
                    goto fail;
                }

                int reg_id = find_reg(out, stmt->as.measure.reg);
                if (reg_id < 0) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "unknown register '%s'",
                                stmt->as.measure.reg);
                    goto fail;
                }

                const QNRegisterInfo *reg = &out->registers[reg_id];
                if (reg->base != 0u || reg->width != out->total_qubits) {
                    qn_diag_set(
                        diag, stmt->line, stmt->column,
                        "v0.2 measure must target the single full register"
                    );
                    goto fail;
                }

                out->capability_mask |= QN_CAP_QUANTUM_SIMULATE;
                measured = true;
                snprintf(measured_name, sizeof(measured_name), "%s",
                         stmt->as.measure.output);
                ins.opcode = QIR_OP_MEASURE_ALL;
                ins.a = register_value(out, (uint16_t)reg_id);
                ins.out = result_value(measured_name);
                if (!append_qir(out, ins, diag)) goto fail;
                break;
            }

            case STMT_EMIT:
                if (!measured ||
                    strcmp(measured_name, stmt->as.emit.name) != 0) {
                    qn_diag_set(
                        diag, stmt->line, stmt->column,
                        "emit must reference the measured result '%s'",
                        measured ? measured_name : "<none>"
                    );
                    goto fail;
                }
                out->capability_mask |= QN_CAP_EVIDENCE_EMIT;
                ins.opcode = QIR_OP_EMIT;
                ins.a = result_value(stmt->as.emit.name);
                if (!append_qir(out, ins, diag)) goto fail;
                break;

            case STMT_REQUIRES: {
                QNCapabilityMask capability = 0;
                if (!qn_capability_parse(
                        stmt->as.requires.capability,
                        &capability)) {
                    qn_diag_set_code(
                        diag,
                        "QN-E-CAP-DECL-001",
                        stmt->line,
                        stmt->column,
                        "unknown capability declaration '%s'",
                        stmt->as.requires.capability
                    );
                    goto fail;
                }
                out->capability_mask |= capability;
                break;
            }

            case STMT_SEED:
                out->default_seed =
                    stmt->as.number.value ? stmt->as.number.value : 1u;
                break;

            case STMT_SHOTS:
                if (stmt->as.number.value == 0u ||
                    stmt->as.number.value > QN_MAX_SHOTS) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "shots must be 1..%u", QN_MAX_SHOTS);
                    goto fail;
                }
                out->default_shots =
                    (uint32_t)stmt->as.number.value;
                break;

            default:
                qn_diag_set(diag, stmt->line, stmt->column,
                            "unsupported statement in typed QIR");
                goto fail;
        }
    }

    if (!measured) {
        qn_diag_set(diag, 1, 1, "program has no measurement");
        goto fail;
    }

    return QN_OK;

fail:
    qn_qir_free(out);
    return QN_ERR_SEMANTIC;
}

static uint8_t global_qubit(const QNQIRProgram *qir,
                            const QNQIRValue *value) {
    const QNRegisterInfo *reg = &qir->registers[value->register_id];
    return (uint8_t)(reg->base + value->qubit_index);
}

QNStatus qn_qir_lower(const QNQIRProgram *qir,
                      QNBytecode *out,
                      QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));
    out->total_qubits = qir->total_qubits;
    out->register_count = qir->register_count;
    out->initial_basis = qir->initial_basis;
    out->default_shots = qir->default_shots;
    out->default_seed = qir->default_seed;
    out->capability_mask = qir->capability_mask;
    memcpy(out->source_digest, qir->source_digest, 32);
    memcpy(out->registers, qir->registers, sizeof(out->registers));

    out->instruction_count = qir->instruction_count + 1u;
    out->instructions = calloc(
        out->instruction_count,
        sizeof(*out->instructions)
    );
    if (!out->instructions) {
        qn_diag_set(diag, 0, 0,
                    "out of memory lowering typed QIR");
        return QN_ERR_QBC;
    }

    for (size_t i = 0; i < qir->instruction_count; ++i) {
        const QNQIRInstruction *src = &qir->instructions[i];
        QNInstruction *dst = &out->instructions[i];

        switch (src->opcode) {
            case QIR_OP_H:
                dst->opcode = OP_H;
                dst->a = global_qubit(qir, &src->a);
                break;
            case QIR_OP_X:
                dst->opcode = OP_X;
                dst->a = global_qubit(qir, &src->a);
                break;
            case QIR_OP_Z:
                dst->opcode = OP_Z;
                dst->a = global_qubit(qir, &src->a);
                break;
            case QIR_OP_CX:
                dst->opcode = OP_CX;
                dst->a = global_qubit(qir, &src->a);
                dst->b = global_qubit(qir, &src->b);
                break;
            case QIR_OP_MEASURE_ALL:
                dst->opcode = OP_MEASURE_ALL;
                break;
            case QIR_OP_EMIT:
                dst->opcode = OP_EMIT;
                break;
            default:
                qn_diag_set(diag, src->line, src->column,
                            "cannot lower QIR opcode %d",
                            (int)src->opcode);
                qn_bytecode_free(out);
                return QN_ERR_QBC;
        }
    }

    out->instructions[out->instruction_count - 1u].opcode = OP_END;
    return QN_OK;
}

static void dump_value(const QNQIRProgram *qir,
                       const QNQIRValue *value,
                       FILE *stream) {
    switch (value->type) {
        case QIR_TYPE_QUBIT:
            fprintf(stream, "qbit %s[%u]@%u",
                    value->name,
                    value->qubit_index,
                    (unsigned)(
                        qir->registers[value->register_id].base +
                        value->qubit_index
                    ));
            break;
        case QIR_TYPE_QREG:
            fprintf(stream, "qreg<%u> %s",
                    qir->registers[value->register_id].width,
                    value->name);
            break;
        case QIR_TYPE_RESULT:
            fprintf(stream, "result %s", value->name);
            break;
        default:
            fprintf(stream, "none");
            break;
    }
}

void qn_qir_dump(const QNQIRProgram *qir, FILE *stream) {
    char source_hex[65];
    qn_hex32(qir->source_digest, source_hex);

    fprintf(stream, "QBIT_NOVA_TYPED_QIR_V02\n");
    fprintf(stream, "qubits=%u\n", qir->total_qubits);
    fprintf(stream, "registers=%u\n", qir->register_count);
    fprintf(stream, "shots=%u\n", qir->default_shots);
    fprintf(stream, "seed=%llu\n",
            (unsigned long long)qir->default_seed);
    fprintf(stream, "source_sha256=%s\n", source_hex);
    char capability_text[256];
    qn_capability_format(
        qir->capability_mask,
        capability_text,
        sizeof(capability_text)
    );
    fprintf(stream, "capabilities=%s\n", capability_text);

    for (uint16_t i = 0; i < qir->register_count; ++i) {
        const QNRegisterInfo *reg = &qir->registers[i];
        fprintf(stream, "REG %u name=%s type=qreg<%u> base=%u\n",
                i, reg->name, reg->width, reg->base);
    }

    for (size_t i = 0; i < qir->instruction_count; ++i) {
        const QNQIRInstruction *ins = &qir->instructions[i];
        fprintf(stream, "%04zu %-12s ", i,
                qn_qir_opcode_name(ins->opcode));

        switch (ins->opcode) {
            case QIR_OP_H:
            case QIR_OP_X:
            case QIR_OP_Z:
                dump_value(qir, &ins->a, stream);
                break;
            case QIR_OP_CX:
                dump_value(qir, &ins->a, stream);
                fprintf(stream, ", ");
                dump_value(qir, &ins->b, stream);
                break;
            case QIR_OP_MEASURE_ALL:
                dump_value(qir, &ins->a, stream);
                fprintf(stream, " -> ");
                dump_value(qir, &ins->out, stream);
                break;
            case QIR_OP_EMIT:
                dump_value(qir, &ins->a, stream);
                break;
            default:
                fprintf(stream, "invalid");
                break;
        }

        fprintf(stream, " ; source=%d:%d\n",
                ins->line, ins->column);
    }
}
