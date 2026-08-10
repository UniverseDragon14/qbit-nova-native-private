#include "qn_qir.h"

#include <stdlib.h>
#include <string.h>

static int find_reg(const QNQIRProgram *qir, const char *name) {
    for (uint16_t i = 0; i < qir->register_count; ++i) {
        if (strcmp(qir->registers[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int find_scalar(const QNQIRProgram *qir, const char *name) {
    for (uint16_t i = 0; i < qir->scalar_count; ++i) {
        if (strcmp(qir->scalars[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int declare_scalar(QNQIRProgram *qir,
                          const char *name,
                          QNQIRType type,
                          int line,
                          int column,
                          QNDiagnostic *diag) {
    if (find_scalar(qir, name) >= 0) {
        qn_diag_set_code(diag, "QN-E7501", line, column,
                         "duplicate scalar variable '%s'", name);
        return -1;
    }
    if (qir->scalar_count >= QN_MAX_SCALARS) {
        qn_diag_set_code(diag, "QN-E7502", line, column,
                         "scalar variable limit exceeded (%u)",
                         QN_MAX_SCALARS);
        return -1;
    }
    uint16_t id = qir->scalar_count++;
    snprintf(qir->scalars[id].name,
             sizeof(qir->scalars[id].name), "%s", name);
    qir->scalars[id].type = type;
    if (type == QIR_TYPE_BOOL) {
        qir->scalar_bool_mask |= UINT64_C(1) << id;
    }
    return (int)id;
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

static QNQIRValue u32_vector_value(const char *name) {
    QNQIRValue value;
    memset(&value, 0, sizeof(value));
    value.type = QIR_TYPE_U32_VECTOR;
    snprintf(value.name, sizeof(value.name), "%s", name);
    return value;
}

static QNQIRValue scalar_value(const QNQIRProgram *qir,
                               uint16_t scalar_id) {
    QNQIRValue value;
    memset(&value, 0, sizeof(value));
    value.type = qir->scalars[scalar_id].type;
    value.register_id = scalar_id;
    snprintf(value.name, sizeof(value.name), "%s",
             qir->scalars[scalar_id].name);
    return value;
}

static unsigned scalar_bool_count(const QNQIRProgram *qir) {
    unsigned count = 0u;
    uint64_t mask = qir->scalar_bool_mask;
    while (mask) {
        count += (unsigned)(mask & UINT64_C(1));
        mask >>= 1;
    }
    return count;
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
        case QIR_TYPE_U32_VECTOR: return "u32vec<256>";
        case QIR_TYPE_U32: return "u32";
        case QIR_TYPE_BOOL: return "bool";
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
        case QIR_OP_U32_VECTOR_ADD: return "U32.VECTOR.ADD";
        case QIR_OP_U32_CONST: return "U32.CONST";
        case QIR_OP_U32_ADD: return "U32.ADD";
        case QIR_OP_U32_SUB: return "U32.SUB";
        case QIR_OP_U32_MUL: return "U32.MUL";
        case QIR_OP_U32_DIV: return "U32.DIV";
        case QIR_OP_U32_EMIT: return "U32.EMIT";
        case QIR_OP_U32_EQ: return "U32.EQ";
        case QIR_OP_U32_NE: return "U32.NE";
        case QIR_OP_U32_LT: return "U32.LT";
        case QIR_OP_U32_LE: return "U32.LE";
        case QIR_OP_U32_GT: return "U32.GT";
        case QIR_OP_U32_GE: return "U32.GE";
        case QIR_OP_BOOL_EMIT: return "BOOL.EMIT";
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
        out->total_qubits = (uint16_t)(out->total_qubits + reg->width);
    }

    bool has_vector_add = false;
    bool has_scalar_statement = false;
    bool has_quantum_statement = false;

    for (size_t i = 0; i < program->count; ++i) {
        switch (program->items[i].kind) {
            case STMT_QREG:
            case STMT_H:
            case STMT_X:
            case STMT_Z:
            case STMT_CX:
            case STMT_GHZ:
            case STMT_MEASURE:
                has_quantum_statement = true;
                break;
            case STMT_VECTOR_ADD_U32:
                has_vector_add = true;
                break;
            case STMT_U32_LET:
            case STMT_U32_ADD:
            case STMT_U32_SUB:
            case STMT_U32_MUL:
            case STMT_U32_DIV:
            case STMT_U32_EQ:
            case STMT_U32_NE:
            case STMT_U32_LT:
            case STMT_U32_LE:
            case STMT_U32_GT:
            case STMT_U32_GE:
                has_scalar_statement = true;
                break;
            default:
                break;
        }
    }

    unsigned modes = (has_quantum_statement ? 1u : 0u) +
                     (has_vector_add ? 1u : 0u) +
                     (has_scalar_statement ? 1u : 0u);
    if (has_quantum_statement && has_vector_add &&
        !has_scalar_statement) {
        qn_diag_set_code(
            diag,
            "QN-E7401",
            1,
            1,
            "bounded vector-add cannot be mixed with quantum statements"
        );
        goto fail;
    }
    if (modes > 1u) {
        qn_diag_set_code(diag, "QN-E7503", 1, 1,
                         "quantum, bounded vector and u32 scalar statements cannot be mixed");
        goto fail;
    }
    if (modes == 0u) {
        qn_diag_set(diag, 1, 1,
                    "program declares no quantum or native compute operation");
        goto fail;
    }
    if ((has_vector_add || has_scalar_statement) && out->total_qubits != 0u) {
        qn_diag_set_code(diag, "QN-E7503", 1, 1,
                         "native compute programs require zero quantum registers");
        goto fail;
    }

    bool measured = false;
    char measured_name[QN_NAME_CAP] = {0};
    bool compute_produced = false;
    bool result_emitted = false;
    char compute_name[QN_NAME_CAP] = {0};

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
                ins.opcode = stmt->kind == STMT_H ? QIR_OP_H :
                             (stmt->kind == STMT_X ? QIR_OP_X : QIR_OP_Z);
                if (!resolve_target(out, &stmt->as.unary.target, &ins.a, diag))
                    goto fail;
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
                if (!resolve_target(out, &stmt->as.cx.control, &ins.a, diag) ||
                    !resolve_target(out, &stmt->as.cx.target, &ins.b, diag))
                    goto fail;
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
                                "unknown register '%s'", stmt->as.ghz.reg);
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
                ins.a = (QNQIRValue){.type=QIR_TYPE_QUBIT,
                                     .register_id=(uint16_t)reg_id,
                                     .qubit_index=0u};
                snprintf(ins.a.name, sizeof(ins.a.name), "%s", reg->name);
                if (!append_qir(out, ins, diag)) goto fail;
                for (uint16_t q = 1u; q < reg->width; ++q) {
                    memset(&ins, 0, sizeof(ins));
                    ins.opcode = QIR_OP_CX;
                    ins.line = stmt->line;
                    ins.column = stmt->column;
                    ins.a = (QNQIRValue){.type=QIR_TYPE_QUBIT,
                                         .register_id=(uint16_t)reg_id,
                                         .qubit_index=0u};
                    ins.b = (QNQIRValue){.type=QIR_TYPE_QUBIT,
                                         .register_id=(uint16_t)reg_id,
                                         .qubit_index=q};
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
                                "unknown register '%s'", stmt->as.measure.reg);
                    goto fail;
                }
                const QNRegisterInfo *reg = &out->registers[reg_id];
                if (reg->base != 0u || reg->width != out->total_qubits) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "v0.2 measure must target the single full register");
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

            case STMT_U32_LET: {
                int id = declare_scalar(out, stmt->as.u32_let.name,
                                        QIR_TYPE_U32,
                                        stmt->line, stmt->column, diag);
                if (id < 0) goto fail;
                out->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                ins.opcode = QIR_OP_U32_CONST;
                ins.out = scalar_value(out, (uint16_t)id);
                ins.imm = stmt->as.u32_let.value;
                if (!append_qir(out, ins, diag)) goto fail;
                break;
            }

            case STMT_U32_ADD:
            case STMT_U32_SUB:
            case STMT_U32_MUL:
            case STMT_U32_DIV: {
                const char *op = stmt->kind == STMT_U32_ADD ? "+" :
                                 stmt->kind == STMT_U32_SUB ? "-" :
                                 stmt->kind == STMT_U32_MUL ? "*" : "/";
                if (find_scalar(out, stmt->as.scalar_binary.output) >= 0) {
                    qn_diag_set_code(diag, "QN-E7501", stmt->line, stmt->column,
                                     "duplicate scalar variable '%s'",
                                     stmt->as.scalar_binary.output);
                    goto fail;
                }
                int left = find_scalar(out, stmt->as.scalar_binary.left);
                int right = find_scalar(out, stmt->as.scalar_binary.right);
                if (left < 0 || right < 0) {
                    qn_diag_set_code(diag, "QN-E7504", stmt->line, stmt->column,
                                     "unknown or uninitialized scalar in '%s = %s %s %s'",
                                     stmt->as.scalar_binary.output,
                                     stmt->as.scalar_binary.left,
                                     op,
                                     stmt->as.scalar_binary.right);
                    goto fail;
                }
                if (out->scalars[left].type != QIR_TYPE_U32 ||
                    out->scalars[right].type != QIR_TYPE_U32) {
                    qn_diag_set_code(diag, "QN-E7521", stmt->line, stmt->column,
                                     "u32 arithmetic '%s' requires u32 operands; got %s and %s",
                                     op,
                                     qn_qir_type_name(out->scalars[left].type),
                                     qn_qir_type_name(out->scalars[right].type));
                    goto fail;
                }
                int output_id = declare_scalar(out, stmt->as.scalar_binary.output,
                                               QIR_TYPE_U32,
                                               stmt->line, stmt->column, diag);
                if (output_id < 0) goto fail;
                out->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                ins.opcode = stmt->kind == STMT_U32_ADD ? QIR_OP_U32_ADD :
                             stmt->kind == STMT_U32_SUB ? QIR_OP_U32_SUB :
                             stmt->kind == STMT_U32_MUL ? QIR_OP_U32_MUL :
                                                           QIR_OP_U32_DIV;
                ins.a = scalar_value(out, (uint16_t)left);
                ins.b = scalar_value(out, (uint16_t)right);
                ins.out = scalar_value(out, (uint16_t)output_id);
                if (!append_qir(out, ins, diag)) goto fail;
                break;
            }

            case STMT_U32_EQ:
            case STMT_U32_NE:
            case STMT_U32_LT:
            case STMT_U32_LE:
            case STMT_U32_GT:
            case STMT_U32_GE: {
                const char *op = stmt->kind == STMT_U32_EQ ? "==" :
                                 stmt->kind == STMT_U32_NE ? "!=" :
                                 stmt->kind == STMT_U32_LT ? "<" :
                                 stmt->kind == STMT_U32_LE ? "<=" :
                                 stmt->kind == STMT_U32_GT ? ">" : ">=";
                if (find_scalar(out, stmt->as.scalar_binary.output) >= 0) {
                    qn_diag_set_code(diag, "QN-E7501", stmt->line, stmt->column,
                                     "duplicate scalar variable '%s'",
                                     stmt->as.scalar_binary.output);
                    goto fail;
                }
                int left = find_scalar(out, stmt->as.scalar_binary.left);
                int right = find_scalar(out, stmt->as.scalar_binary.right);
                if (left < 0 || right < 0) {
                    qn_diag_set_code(diag, "QN-E7504", stmt->line, stmt->column,
                                     "unknown or uninitialized scalar in '%s = %s %s %s'",
                                     stmt->as.scalar_binary.output,
                                     stmt->as.scalar_binary.left,
                                     op,
                                     stmt->as.scalar_binary.right);
                    goto fail;
                }
                if (out->scalars[left].type != QIR_TYPE_U32 ||
                    out->scalars[right].type != QIR_TYPE_U32) {
                    qn_diag_set_code(diag, "QN-E7522", stmt->line, stmt->column,
                                     "u32 comparison '%s' requires u32 operands; got %s and %s",
                                     op,
                                     qn_qir_type_name(out->scalars[left].type),
                                     qn_qir_type_name(out->scalars[right].type));
                    goto fail;
                }
                int output_id = declare_scalar(out, stmt->as.scalar_binary.output,
                                               QIR_TYPE_BOOL,
                                               stmt->line, stmt->column, diag);
                if (output_id < 0) goto fail;
                out->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                ins.opcode = stmt->kind == STMT_U32_EQ ? QIR_OP_U32_EQ :
                             stmt->kind == STMT_U32_NE ? QIR_OP_U32_NE :
                             stmt->kind == STMT_U32_LT ? QIR_OP_U32_LT :
                             stmt->kind == STMT_U32_LE ? QIR_OP_U32_LE :
                             stmt->kind == STMT_U32_GT ? QIR_OP_U32_GT :
                                                         QIR_OP_U32_GE;
                ins.a = scalar_value(out, (uint16_t)left);
                ins.b = scalar_value(out, (uint16_t)right);
                ins.out = scalar_value(out, (uint16_t)output_id);
                if (!append_qir(out, ins, diag)) goto fail;
                break;
            }

            case STMT_EMIT:
                if (result_emitted) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "only one emitted result is allowed");
                    goto fail;
                }
                if (has_scalar_statement) {
                    int id = find_scalar(out, stmt->as.emit.name);
                    if (id < 0) {
                        qn_diag_set_code(diag, "QN-E7504",
                                         stmt->line, stmt->column,
                                         "emit references unknown scalar variable '%s'",
                                         stmt->as.emit.name);
                        goto fail;
                    }
                    ins.a = scalar_value(out, (uint16_t)id);
                    ins.opcode = ins.a.type == QIR_TYPE_BOOL
                        ? QIR_OP_BOOL_EMIT
                        : QIR_OP_U32_EMIT;
                } else if (has_vector_add) {
                    if (!compute_produced ||
                        strcmp(compute_name, stmt->as.emit.name) != 0) {
                        qn_diag_set(diag, stmt->line, stmt->column,
                                    "emit must reference bounded compute result '%s'",
                                    compute_produced ? compute_name : "<none>");
                        goto fail;
                    }
                    ins.opcode = QIR_OP_EMIT;
                    ins.a = u32_vector_value(stmt->as.emit.name);
                } else {
                    if (!measured ||
                        strcmp(measured_name, stmt->as.emit.name) != 0) {
                        qn_diag_set(diag, stmt->line, stmt->column,
                                    "emit must reference the measured result '%s'",
                                    measured ? measured_name : "<none>");
                        goto fail;
                    }
                    ins.opcode = QIR_OP_EMIT;
                    ins.a = result_value(stmt->as.emit.name);
                }
                out->capability_mask |= QN_CAP_EVIDENCE_EMIT;
                result_emitted = true;
                if (!append_qir(out, ins, diag)) goto fail;
                break;

            case STMT_VECTOR_ADD_U32:
                if (compute_produced) {
                    qn_diag_set_code(diag, "QN-E7402", stmt->line, stmt->column,
                                     "only one bounded uint32 vector-add is allowed");
                    goto fail;
                }
                compute_produced = true;
                snprintf(compute_name, sizeof(compute_name), "%s",
                         stmt->as.vector_add_u32.output);
                out->capability_mask |= QN_CAP_COMPUTE_U32_ADD;
                ins.opcode = QIR_OP_U32_VECTOR_ADD;
                ins.a = u32_vector_value("fixed_input_a");
                ins.b = u32_vector_value("fixed_input_b");
                ins.out = u32_vector_value(compute_name);
                if (!append_qir(out, ins, diag)) goto fail;
                break;

            case STMT_REQUIRES: {
                QNCapabilityMask capability = 0;
                if (!qn_capability_parse(stmt->as.requires.capability,
                                         &capability)) {
                    qn_diag_set_code(diag, "QN-E-CAP-DECL-001",
                                     stmt->line, stmt->column,
                                     "unknown capability declaration '%s'",
                                     stmt->as.requires.capability);
                    goto fail;
                }
                out->capability_mask |= capability;
                break;
            }

            case STMT_SEED:
                if (has_vector_add || has_scalar_statement) {
                    qn_diag_set_code(diag,
                                     has_scalar_statement ? "QN-E7505" : "QN-E7403",
                                     stmt->line, stmt->column,
                                     "seed is not valid for native deterministic compute");
                    goto fail;
                }
                out->default_seed = stmt->as.number.value
                    ? stmt->as.number.value : 1u;
                break;

            case STMT_SHOTS:
                if (has_vector_add || has_scalar_statement) {
                    qn_diag_set_code(diag,
                                     has_scalar_statement ? "QN-E7505" : "QN-E7403",
                                     stmt->line, stmt->column,
                                     "shots is not valid for native deterministic compute");
                    goto fail;
                }
                if (stmt->as.number.value == 0u ||
                    stmt->as.number.value > QN_MAX_SHOTS) {
                    qn_diag_set(diag, stmt->line, stmt->column,
                                "shots must be 1..%u", QN_MAX_SHOTS);
                    goto fail;
                }
                out->default_shots = (uint32_t)stmt->as.number.value;
                break;

            default:
                qn_diag_set(diag, stmt->line, stmt->column,
                            "unsupported statement in typed QIR");
                goto fail;
        }
    }

    if (has_scalar_statement) {
        QNCapabilityMask allowed =
            QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
        if ((out->capability_mask & ~allowed) != 0u) {
            qn_diag_set_code(diag, "QN-E7506", 1, 1,
                             "u32 scalar programs permit only compute.u32_scalar and evidence.emit");
            goto fail;
        }
        if (out->scalar_count == 0u) {
            qn_diag_set_code(diag, "QN-E7507", 1, 1,
                             "u32 scalar program declares no variables");
            goto fail;
        }
        if (!result_emitted) {
            qn_diag_set_code(diag, "QN-E7508", 1, 1,
                             "u32 scalar result must be emitted");
            goto fail;
        }
    } else if (has_vector_add) {
        QNCapabilityMask allowed =
            QN_CAP_COMPUTE_U32_ADD | QN_CAP_EVIDENCE_EMIT;
        if ((out->capability_mask & ~allowed) != 0u) {
            qn_diag_set_code(diag, "QN-E7405", 1, 1,
                             "bounded vector-add permits only compute.u32_vector_add and evidence.emit");
            goto fail;
        }
        if (!compute_produced) {
            qn_diag_set_code(diag, "QN-E7402", 1, 1,
                             "program has no bounded vector-add operation");
            goto fail;
        }
        if (!result_emitted) {
            qn_diag_set_code(diag, "QN-E7404", 1, 1,
                             "bounded vector-add result must be emitted");
            goto fail;
        }
    } else {
        if (!measured) {
            qn_diag_set(diag, 1, 1, "program has no measurement");
            goto fail;
        }
        if (!result_emitted) {
            qn_diag_set(diag, 1, 1, "measured result must be emitted");
            goto fail;
        }
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
    out->scalar_count = qir->scalar_count;
    out->scalar_bool_mask = qir->scalar_bool_mask;
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
            case QIR_OP_U32_VECTOR_ADD:
                dst->opcode = OP_U32_VECTOR_ADD;
                dst->imm = QN_U32_VECTOR_ADD_COUNT;
                break;
            case QIR_OP_U32_CONST:
                dst->opcode = OP_U32_CONST;
                dst->a = (uint8_t)src->out.register_id;
                dst->imm = src->imm;
                break;
            case QIR_OP_U32_ADD:
                dst->opcode = OP_U32_ADD;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_SUB:
                dst->opcode = OP_U32_SUB;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_MUL:
                dst->opcode = OP_U32_MUL;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_DIV:
                dst->opcode = OP_U32_DIV;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_EMIT:
                dst->opcode = OP_U32_EMIT;
                dst->a = (uint8_t)src->a.register_id;
                break;
            case QIR_OP_U32_EQ:
                dst->opcode = OP_U32_EQ;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_NE:
                dst->opcode = OP_U32_NE;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_LT:
                dst->opcode = OP_U32_LT;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_LE:
                dst->opcode = OP_U32_LE;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_GT:
                dst->opcode = OP_U32_GT;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_U32_GE:
                dst->opcode = OP_U32_GE;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_BOOL_EMIT:
                dst->opcode = OP_BOOL_EMIT;
                dst->a = (uint8_t)src->a.register_id;
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
        case QIR_TYPE_U32_VECTOR:
            fprintf(stream, "u32vec<%u> %s",
                    QN_U32_VECTOR_ADD_COUNT, value->name);
            break;
        case QIR_TYPE_U32:
            fprintf(stream, "u32 %s@%u", value->name,
                    value->register_id);
            break;
        case QIR_TYPE_BOOL:
            fprintf(stream, "bool %s@%u", value->name,
                    value->register_id);
            break;
        default:
            fprintf(stream, "none");
            break;
    }
}

void qn_qir_dump(const QNQIRProgram *qir, FILE *stream) {
    char source_hex[65];
    qn_hex32(qir->source_digest, source_hex);

    unsigned bool_count = scalar_bool_count(qir);
    if (bool_count > 0u) {
        fprintf(stream, "QBIT_NOVA_TYPED_QIR_V03\n");
    } else {
        fprintf(stream, "QBIT_NOVA_TYPED_QIR_V02\n");
    }
    fprintf(stream, "qubits=%u\n", qir->total_qubits);
    fprintf(stream, "registers=%u\n", qir->register_count);
    if (bool_count > 0u) {
        fprintf(stream, "scalar_slots=%u\n", qir->scalar_count);
        fprintf(stream, "u32_scalars=%u\n",
                (unsigned)qir->scalar_count - bool_count);
        fprintf(stream, "bool_scalars=%u\n", bool_count);
        fprintf(stream, "scalar_bool_mask=0x%016llx\n",
                (unsigned long long)qir->scalar_bool_mask);
    } else {
        fprintf(stream, "u32_scalars=%u\n", qir->scalar_count);
    }
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
            case QIR_OP_U32_VECTOR_ADD:
                dump_value(qir, &ins->a, stream);
                fprintf(stream, ", ");
                dump_value(qir, &ins->b, stream);
                fprintf(stream, " -> ");
                dump_value(qir, &ins->out, stream);
                break;
            case QIR_OP_U32_CONST:
                fprintf(stream, "%u -> ", ins->imm);
                dump_value(qir, &ins->out, stream);
                break;
            case QIR_OP_U32_ADD:
            case QIR_OP_U32_SUB:
            case QIR_OP_U32_MUL:
            case QIR_OP_U32_DIV:
            case QIR_OP_U32_EQ:
            case QIR_OP_U32_NE:
            case QIR_OP_U32_LT:
            case QIR_OP_U32_LE:
            case QIR_OP_U32_GT:
            case QIR_OP_U32_GE:
                dump_value(qir, &ins->a, stream);
                fprintf(stream, ", ");
                dump_value(qir, &ins->b, stream);
                fprintf(stream, " -> ");
                dump_value(qir, &ins->out, stream);
                break;
            case QIR_OP_U32_EMIT:
            case QIR_OP_BOOL_EMIT:
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
