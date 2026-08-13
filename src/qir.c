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


typedef struct {
    char name[QN_NAME_CAP];
    uint16_t id;
    QNQIRType type;
} QNBranchBinding;

typedef struct {
    QNBranchBinding items[QN_MAX_SCALARS];
    uint16_t count;
} QNBranchScope;

static void branch_scope_init(QNBranchScope *scope,
                              const QNQIRProgram *qir,
                              uint16_t outer_count) {
    memset(scope, 0, sizeof(*scope));
    for (uint16_t i = 0; i < outer_count; ++i) {
        QNBranchBinding *binding = &scope->items[scope->count++];
        snprintf(binding->name, sizeof(binding->name), "%s",
                 qir->scalars[i].name);
        binding->id = i;
        binding->type = qir->scalars[i].type;
    }
}

static int branch_scope_find(const QNBranchScope *scope,
                             const char *name) {
    for (uint16_t i = 0; i < scope->count; ++i) {
        if (strcmp(scope->items[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int branch_scope_declare(QNQIRProgram *qir,
                                QNBranchScope *scope,
                                const char *name,
                                QNQIRType type,
                                int line,
                                int column,
                                QNDiagnostic *diag) {
    if (branch_scope_find(scope, name) >= 0) {
        qn_diag_set_code(diag, "QN-E7501", line, column,
                         "duplicate scalar variable '%s'", name);
        return -1;
    }
    if (qir->scalar_count >= QN_MAX_SCALARS ||
        scope->count >= QN_MAX_SCALARS) {
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

    QNBranchBinding *binding = &scope->items[scope->count++];
    snprintf(binding->name, sizeof(binding->name), "%s", name);
    binding->id = id;
    binding->type = type;
    return (int)id;
}

static QNQIRValue branch_scalar_value(const QNQIRProgram *qir,
                                      const QNBranchBinding *binding) {
    return scalar_value(qir, binding->id);
}

static bool compile_scalar_branch(QNQIRProgram *qir,
                                  const QNStmt *items,
                                  size_t count,
                                  uint16_t outer_count,
                                  QNQIRType *emitted_type,
                                  QNDiagnostic *diag) {
    QNBranchScope scope;
    branch_scope_init(&scope, qir, outer_count);
    bool emitted = false;
    *emitted_type = QIR_TYPE_NONE;

    for (size_t i = 0; i < count; ++i) {
        const QNStmt *stmt = &items[i];
        QNQIRInstruction ins;
        memset(&ins, 0, sizeof(ins));
        ins.line = stmt->line;
        ins.column = stmt->column;

        if (emitted) {
            qn_diag_set_code(diag, "QN-E7534", stmt->line, stmt->column,
                             "if/else branch must terminate at emit");
            return false;
        }

        switch (stmt->kind) {
            case STMT_U32_LET: {
                int id = branch_scope_declare(
                    qir, &scope, stmt->as.u32_let.name,
                    QIR_TYPE_U32, stmt->line, stmt->column, diag
                );
                if (id < 0) return false;
                qir->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                ins.opcode = QIR_OP_U32_CONST;
                ins.out = scalar_value(qir, (uint16_t)id);
                ins.imm = stmt->as.u32_let.value;
                if (!append_qir(qir, ins, diag)) return false;
                break;
            }

            case STMT_U32_ADD:
            case STMT_U32_SUB:
            case STMT_U32_MUL:
            case STMT_U32_DIV: {
                const char *op = stmt->kind == STMT_U32_ADD ? "+" :
                                 stmt->kind == STMT_U32_SUB ? "-" :
                                 stmt->kind == STMT_U32_MUL ? "*" : "/";
                int left_index = branch_scope_find(
                    &scope, stmt->as.scalar_binary.left
                );
                int right_index = branch_scope_find(
                    &scope, stmt->as.scalar_binary.right
                );
                if (left_index < 0 || right_index < 0) {
                    qn_diag_set_code(diag, "QN-E7504",
                                     stmt->line, stmt->column,
                                     "unknown or uninitialized scalar in '%s = %s %s %s'",
                                     stmt->as.scalar_binary.output,
                                     stmt->as.scalar_binary.left,
                                     op,
                                     stmt->as.scalar_binary.right);
                    return false;
                }
                const QNBranchBinding *left = &scope.items[left_index];
                const QNBranchBinding *right = &scope.items[right_index];
                if (left->type != QIR_TYPE_U32 ||
                    right->type != QIR_TYPE_U32) {
                    qn_diag_set_code(diag, "QN-E7521",
                                     stmt->line, stmt->column,
                                     "u32 arithmetic '%s' requires u32 operands; got %s and %s",
                                     op,
                                     qn_qir_type_name(left->type),
                                     qn_qir_type_name(right->type));
                    return false;
                }
                int id = branch_scope_declare(
                    qir, &scope, stmt->as.scalar_binary.output,
                    QIR_TYPE_U32, stmt->line, stmt->column, diag
                );
                if (id < 0) return false;
                qir->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                ins.opcode = stmt->kind == STMT_U32_ADD ? QIR_OP_U32_ADD :
                             stmt->kind == STMT_U32_SUB ? QIR_OP_U32_SUB :
                             stmt->kind == STMT_U32_MUL ? QIR_OP_U32_MUL :
                                                           QIR_OP_U32_DIV;
                ins.a = branch_scalar_value(qir, left);
                ins.b = branch_scalar_value(qir, right);
                ins.out = scalar_value(qir, (uint16_t)id);
                if (!append_qir(qir, ins, diag)) return false;
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
                int left_index = branch_scope_find(
                    &scope, stmt->as.scalar_binary.left
                );
                int right_index = branch_scope_find(
                    &scope, stmt->as.scalar_binary.right
                );
                if (left_index < 0 || right_index < 0) {
                    qn_diag_set_code(diag, "QN-E7504",
                                     stmt->line, stmt->column,
                                     "unknown or uninitialized scalar in '%s = %s %s %s'",
                                     stmt->as.scalar_binary.output,
                                     stmt->as.scalar_binary.left,
                                     op,
                                     stmt->as.scalar_binary.right);
                    return false;
                }
                const QNBranchBinding *left = &scope.items[left_index];
                const QNBranchBinding *right = &scope.items[right_index];
                if (left->type != QIR_TYPE_U32 ||
                    right->type != QIR_TYPE_U32) {
                    qn_diag_set_code(diag, "QN-E7522",
                                     stmt->line, stmt->column,
                                     "u32 comparison '%s' requires u32 operands; got %s and %s",
                                     op,
                                     qn_qir_type_name(left->type),
                                     qn_qir_type_name(right->type));
                    return false;
                }
                int id = branch_scope_declare(
                    qir, &scope, stmt->as.scalar_binary.output,
                    QIR_TYPE_BOOL, stmt->line, stmt->column, diag
                );
                if (id < 0) return false;
                qir->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                ins.opcode = stmt->kind == STMT_U32_EQ ? QIR_OP_U32_EQ :
                             stmt->kind == STMT_U32_NE ? QIR_OP_U32_NE :
                             stmt->kind == STMT_U32_LT ? QIR_OP_U32_LT :
                             stmt->kind == STMT_U32_LE ? QIR_OP_U32_LE :
                             stmt->kind == STMT_U32_GT ? QIR_OP_U32_GT :
                                                         QIR_OP_U32_GE;
                ins.a = branch_scalar_value(qir, left);
                ins.b = branch_scalar_value(qir, right);
                ins.out = scalar_value(qir, (uint16_t)id);
                if (!append_qir(qir, ins, diag)) return false;
                break;
            }

            case STMT_EMIT: {
                int binding_index = branch_scope_find(
                    &scope, stmt->as.emit.name
                );
                if (binding_index < 0) {
                    qn_diag_set_code(diag, "QN-E7504",
                                     stmt->line, stmt->column,
                                     "emit references unknown scalar variable '%s'",
                                     stmt->as.emit.name);
                    return false;
                }
                const QNBranchBinding *binding =
                    &scope.items[binding_index];
                ins.a = branch_scalar_value(qir, binding);
                ins.opcode = binding->type == QIR_TYPE_BOOL
                    ? QIR_OP_BOOL_EMIT
                    : QIR_OP_U32_EMIT;
                qir->capability_mask |= QN_CAP_EVIDENCE_EMIT;
                if (!append_qir(qir, ins, diag)) return false;
                emitted = true;
                *emitted_type = binding->type;
                break;
            }

            case STMT_IF:
                qn_diag_set_code(diag, "QN-E7536",
                                 stmt->line, stmt->column,
                                 "nested if is not enabled in Stage 7 Step 4");
                return false;

            default:
                qn_diag_set_code(diag, "QN-E7537",
                                 stmt->line, stmt->column,
                                 "unsupported statement inside Step4 if/else block");
                return false;
        }
    }

    if (!emitted) {
        qn_diag_set_code(diag, "QN-E7534", 1, 1,
                         "each Step4 if/else branch must terminate with emit");
        return false;
    }
    return true;
}

static bool compile_repeat_body(QNQIRProgram *qir,
                                const QNStmt *items,
                                size_t count,
                                QNDiagnostic *diag) {
    for (size_t i = 0; i < count; ++i) {
        const QNStmt *stmt = &items[i];
        QNQIRInstruction ins;
        memset(&ins, 0, sizeof(ins));
        ins.line = stmt->line;
        ins.column = stmt->column;

        if (stmt->kind != STMT_U32_SET_ADD &&
            stmt->kind != STMT_U32_SET_SUB &&
            stmt->kind != STMT_U32_SET_MUL &&
            stmt->kind != STMT_U32_SET_DIV) {
            qn_diag_set_code(diag, "QN-E7559", stmt->line, stmt->column,
                             "repeat body contains unsupported statement");
            return false;
        }

        int target = find_scalar(qir, stmt->as.scalar_binary.output);
        if (target < 0) {
            qn_diag_set_code(diag, "QN-E7560", stmt->line, stmt->column,
                             "set target references unknown scalar '%s'",
                             stmt->as.scalar_binary.output);
            return false;
        }
        int left = find_scalar(qir, stmt->as.scalar_binary.left);
        int right = find_scalar(qir, stmt->as.scalar_binary.right);
        if (left < 0 || right < 0) {
            qn_diag_set_code(diag, "QN-E7561", stmt->line, stmt->column,
                             "set reads unknown scalar in '%s = %s op %s'",
                             stmt->as.scalar_binary.output,
                             stmt->as.scalar_binary.left,
                             stmt->as.scalar_binary.right);
            return false;
        }
        if (qir->scalars[target].type != QIR_TYPE_U32 ||
            qir->scalars[left].type != QIR_TYPE_U32 ||
            qir->scalars[right].type != QIR_TYPE_U32) {
            qn_diag_set_code(diag, "QN-E7562", stmt->line, stmt->column,
                             "set target and operands must all be u32");
            return false;
        }

        ins.opcode = stmt->kind == STMT_U32_SET_ADD ? QIR_OP_U32_SET_ADD :
                     stmt->kind == STMT_U32_SET_SUB ? QIR_OP_U32_SET_SUB :
                     stmt->kind == STMT_U32_SET_MUL ? QIR_OP_U32_SET_MUL :
                                                       QIR_OP_U32_SET_DIV;
        ins.out = scalar_value(qir, (uint16_t)target);
        ins.a = scalar_value(qir, (uint16_t)left);
        ins.b = scalar_value(qir, (uint16_t)right);
        if (!append_qir(qir, ins, diag)) return false;
    }
    return true;
}


typedef struct {
    QNScalarInfo scalars[QN_MAX_SCALARS];
    uint16_t count;
} QNFunctionScope;

static int function_scope_find(const QNFunctionScope *scope,
                               const char *name) {
    for (uint16_t i = 0u; i < scope->count; ++i) {
        if (strcmp(scope->scalars[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int function_scope_declare(QNFunctionScope *scope,
                                  const char *name,
                                  int line,
                                  int column,
                                  QNDiagnostic *diag) {
    if (function_scope_find(scope, name) >= 0) {
        qn_diag_set_code(diag, "QN-E7501", line, column,
                         "duplicate scalar variable '%s'", name);
        return -1;
    }
    if (scope->count >= QN_MAX_SCALARS) {
        qn_diag_set_code(diag, "QN-E7502", line, column,
                         "scalar variable limit exceeded (%u)", QN_MAX_SCALARS);
        return -1;
    }
    uint16_t id = scope->count++;
    snprintf(scope->scalars[id].name,
             sizeof(scope->scalars[id].name), "%s", name);
    scope->scalars[id].type = QIR_TYPE_U32;
    return (int)id;
}

static QNQIRValue function_scalar_value(const QNFunctionScope *scope,
                                        uint16_t id) {
    QNQIRValue value;
    memset(&value, 0, sizeof(value));
    value.type = QIR_TYPE_U32;
    value.register_id = id;
    snprintf(value.name, sizeof(value.name), "%s", scope->scalars[id].name);
    return value;
}

static int find_function_decl(const QNProgram *program, const char *name) {
    for (size_t i = 0u; i < program->function_count; ++i) {
        if (strcmp(program->functions[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool compile_function_call(QNQIRProgram *qir,
                                  const QNProgram *program,
                                  QNFunctionScope *scope,
                                  const QNStmt *stmt,
                                  bool call_graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS],
                                  int caller_function,
                                  bool *saw_call,
                                  QNDiagnostic *diag) {
    int callee = find_function_decl(program, stmt->as.call.function);
    if (callee < 0) {
        qn_diag_set_code(diag, "QN-E7583", stmt->line, stmt->column,
                         "unknown function '%s'", stmt->as.call.function);
        return false;
    }
    const QNFunctionDecl *fn = &program->functions[callee];
    if (stmt->as.call.arg_count != fn->param_count) {
        qn_diag_set_code(diag, "QN-E7584", stmt->line, stmt->column,
                         "function '%s' expects %u arguments, got %u",
                         fn->name, fn->param_count, stmt->as.call.arg_count);
        return false;
    }
    if (function_scope_find(scope, stmt->as.call.output) >= 0) {
        qn_diag_set_code(diag, "QN-E7501", stmt->line, stmt->column,
                         "duplicate scalar variable '%s'", stmt->as.call.output);
        return false;
    }

    int arg_ids[QN_MAX_FUNCTION_PARAMS] = {-1, -1};
    for (uint8_t i = 0u; i < stmt->as.call.arg_count; ++i) {
        arg_ids[i] = function_scope_find(scope, stmt->as.call.args[i]);
        if (arg_ids[i] < 0) {
            qn_diag_set_code(diag, "QN-E7585", stmt->line, stmt->column,
                             "call to '%s' references unknown u32 argument '%s'",
                             fn->name, stmt->as.call.args[i]);
            return false;
        }
    }

    int output = function_scope_declare(scope, stmt->as.call.output,
                                        stmt->line, stmt->column, diag);
    if (output < 0) return false;

    QNQIRInstruction ins;
    memset(&ins, 0, sizeof(ins));
    ins.line = stmt->line;
    ins.column = stmt->column;
    ins.opcode = QIR_OP_CALL;
    ins.out = function_scalar_value(scope, (uint16_t)output);
    if (stmt->as.call.arg_count > 0u) {
        ins.a = function_scalar_value(scope, (uint16_t)arg_ids[0]);
    }
    if (stmt->as.call.arg_count > 1u) {
        ins.b = function_scalar_value(scope, (uint16_t)arg_ids[1]);
    }
    ins.imm = (uint32_t)callee;
    if (!append_qir(qir, ins, diag)) return false;

    if (caller_function >= 0) {
        call_graph[caller_function][callee] = true;
    }
    *saw_call = true;
    return true;
}

static bool compile_function_body(QNQIRProgram *qir,
                                  const QNProgram *program,
                                  size_t function_index,
                                  bool call_graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS],
                                  QNDiagnostic *diag) {
    const QNFunctionDecl *fn = &program->functions[function_index];
    QNFunctionScope scope;
    memset(&scope, 0, sizeof(scope));

    for (uint8_t i = 0u; i < fn->param_count; ++i) {
        if (function_scope_declare(&scope, fn->params[i],
                                   fn->line, fn->column, diag) < 0) {
            return false;
        }
    }

    QNQIRFunctionInfo *meta = &qir->functions[function_index];
    memset(meta, 0, sizeof(*meta));
    snprintf(meta->name, sizeof(meta->name), "%s", fn->name);
    meta->param_count = fn->param_count;
    if (qir->instruction_count > UINT32_MAX) return false;
    meta->entry_instruction = (uint32_t)qir->instruction_count;

    bool returned = false;
    bool saw_call = false;
    for (size_t i = 0u; i < fn->body_count; ++i) {
        const QNStmt *stmt = &fn->body_items[i];
        QNQIRInstruction ins;
        memset(&ins, 0, sizeof(ins));
        ins.line = stmt->line;
        ins.column = stmt->column;

        if (returned) {
            qn_diag_set_code(diag, "QN-E7578", stmt->line, stmt->column,
                             "return must be the final function statement");
            return false;
        }

        switch (stmt->kind) {
            case STMT_U32_LET: {
                int id = function_scope_declare(&scope, stmt->as.u32_let.name,
                                                stmt->line, stmt->column, diag);
                if (id < 0) return false;
                ins.opcode = QIR_OP_U32_CONST;
                ins.out = function_scalar_value(&scope, (uint16_t)id);
                ins.imm = stmt->as.u32_let.value;
                if (!append_qir(qir, ins, diag)) return false;
                break;
            }
            case STMT_U32_ADD:
            case STMT_U32_SUB:
            case STMT_U32_MUL:
            case STMT_U32_DIV: {
                int left = function_scope_find(&scope, stmt->as.scalar_binary.left);
                int right = function_scope_find(&scope, stmt->as.scalar_binary.right);
                if (left < 0 || right < 0) {
                    qn_diag_set_code(diag, "QN-E7504", stmt->line, stmt->column,
                                     "function arithmetic reads unknown u32 scalar");
                    return false;
                }
                int output = function_scope_declare(&scope,
                                                    stmt->as.scalar_binary.output,
                                                    stmt->line, stmt->column, diag);
                if (output < 0) return false;
                ins.opcode = stmt->kind == STMT_U32_ADD ? QIR_OP_U32_ADD :
                             stmt->kind == STMT_U32_SUB ? QIR_OP_U32_SUB :
                             stmt->kind == STMT_U32_MUL ? QIR_OP_U32_MUL :
                                                          QIR_OP_U32_DIV;
                ins.a = function_scalar_value(&scope, (uint16_t)left);
                ins.b = function_scalar_value(&scope, (uint16_t)right);
                ins.out = function_scalar_value(&scope, (uint16_t)output);
                if (!append_qir(qir, ins, diag)) return false;
                break;
            }
            case STMT_CALL:
                if (!compile_function_call(qir, program, &scope, stmt,
                                           call_graph, (int)function_index,
                                           &saw_call, diag)) return false;
                break;
            case STMT_RETURN: {
                int id = function_scope_find(&scope, stmt->as.return_stmt.name);
                if (id < 0) {
                    qn_diag_set_code(diag, "QN-E7586", stmt->line, stmt->column,
                                     "return references unknown u32 scalar '%s'",
                                     stmt->as.return_stmt.name);
                    return false;
                }
                ins.opcode = QIR_OP_RETURN;
                ins.a = function_scalar_value(&scope, (uint16_t)id);
                if (!append_qir(qir, ins, diag)) return false;
                returned = true;
                break;
            }
            default:
                qn_diag_set_code(diag, "QN-E7575", stmt->line, stmt->column,
                                 "unsupported statement inside Step6 function");
                return false;
        }
    }

    (void)saw_call;
    if (!returned) {
        qn_diag_set_code(diag, "QN-E7579", fn->line, fn->column,
                         "Step6 function '%s' must terminate with return", fn->name);
        return false;
    }
    if (qir->instruction_count > UINT32_MAX) return false;
    meta->end_instruction = (uint32_t)qir->instruction_count;
    meta->scalar_count = scope.count;
    return true;
}

static bool function_graph_depth_dfs(size_t node,
                                     size_t function_count,
                                     bool graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS],
                                     uint8_t state[QN_MAX_FUNCTIONS],
                                     uint8_t memo[QN_MAX_FUNCTIONS],
                                     uint8_t *depth_out,
                                     QNDiagnostic *diag) {
    if (state[node] == 1u) {
        qn_diag_set_code(diag, "QN-E7587", 0, 0,
                         "recursive function call cycle is forbidden in Step6");
        return false;
    }
    if (state[node] == 2u) {
        *depth_out = memo[node];
        return true;
    }
    state[node] = 1u;
    uint8_t depth = 1u;
    for (size_t callee = 0u; callee < function_count; ++callee) {
        if (!graph[node][callee]) continue;
        uint8_t child = 0u;
        if (!function_graph_depth_dfs(callee, function_count, graph,
                                     state, memo, &child, diag)) return false;
        if ((uint16_t)child + 1u > depth) depth = (uint8_t)(child + 1u);
    }
    state[node] = 2u;
    memo[node] = depth;
    *depth_out = depth;
    return true;
}

static bool validate_function_call_graph(size_t function_count,
                                         bool graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS],
                                         QNDiagnostic *diag) {
    uint8_t state[QN_MAX_FUNCTIONS] = {0};
    uint8_t memo[QN_MAX_FUNCTIONS] = {0};
    for (size_t i = 0u; i < function_count; ++i) {
        uint8_t depth = 0u;
        if (!function_graph_depth_dfs(i, function_count, graph,
                                     state, memo, &depth, diag)) return false;
        if (depth > QN_MAX_CALL_DEPTH) {
            qn_diag_set_code(diag, "QN-E7588", 0, 0,
                             "function call depth %u exceeds Step6 limit %u",
                             depth, QN_MAX_CALL_DEPTH);
            return false;
        }
    }
    return true;
}

static QNStatus qn_qir_build_function_program(const QNProgram *program,
                                               const uint8_t source_digest[32],
                                               QNQIRProgram *out,
                                               QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));
    memcpy(out->source_digest, source_digest, 32);
    out->default_shots = 1u;
    out->default_seed = 1u;
    if (program->function_count > QN_MAX_FUNCTIONS) {
        qn_diag_set_code(diag, "QN-E7570", 1, 1,
                         "invalid Step6 function count");
        return QN_ERR_SEMANTIC;
    }

    bool call_graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS] = {{false}};
    out->function_count = (uint16_t)program->function_count;
    for (size_t i = 0u; i < program->function_count; ++i) {
        if (!compile_function_body(out, program, i, call_graph, diag)) goto fail;
    }
    if (!validate_function_call_graph(program->function_count,
                                      call_graph, diag)) goto fail;

    if (out->instruction_count > UINT32_MAX) goto fail;
    out->main_entry_instruction = (uint32_t)out->instruction_count;
    bool saw_call = false;
    bool emitted = false;

    for (size_t i = 0u; i < program->count; ++i) {
        const QNStmt *stmt = &program->items[i];
        QNQIRInstruction ins;
        memset(&ins, 0, sizeof(ins));
        ins.line = stmt->line;
        ins.column = stmt->column;

        if (emitted) {
            qn_diag_set_code(diag, "QN-E7589", stmt->line, stmt->column,
                             "Step6 main must terminate at emit");
            goto fail;
        }

        switch (stmt->kind) {
            case STMT_U32_LET: {
                int id = declare_scalar(out, stmt->as.u32_let.name,
                                        QIR_TYPE_U32,
                                        stmt->line, stmt->column, diag);
                if (id < 0) goto fail;
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
                int left = find_scalar(out, stmt->as.scalar_binary.left);
                int right = find_scalar(out, stmt->as.scalar_binary.right);
                if (left < 0 || right < 0 ||
                    out->scalars[left].type != QIR_TYPE_U32 ||
                    out->scalars[right].type != QIR_TYPE_U32) {
                    qn_diag_set_code(diag, "QN-E7504", stmt->line, stmt->column,
                                     "Step6 main arithmetic requires initialized u32 operands");
                    goto fail;
                }
                int output = declare_scalar(out, stmt->as.scalar_binary.output,
                                            QIR_TYPE_U32,
                                            stmt->line, stmt->column, diag);
                if (output < 0) goto fail;
                ins.opcode = stmt->kind == STMT_U32_ADD ? QIR_OP_U32_ADD :
                             stmt->kind == STMT_U32_SUB ? QIR_OP_U32_SUB :
                             stmt->kind == STMT_U32_MUL ? QIR_OP_U32_MUL :
                                                          QIR_OP_U32_DIV;
                ins.a = scalar_value(out, (uint16_t)left);
                ins.b = scalar_value(out, (uint16_t)right);
                ins.out = scalar_value(out, (uint16_t)output);
                if (!append_qir(out, ins, diag)) goto fail;
                break;
            }
            case STMT_CALL: {
                QNFunctionScope scope;
                memset(&scope, 0, sizeof(scope));
                scope.count = out->scalar_count;
                memcpy(scope.scalars, out->scalars,
                       out->scalar_count * sizeof(out->scalars[0]));
                if (!compile_function_call(out, program, &scope, stmt,
                                           call_graph, -1, &saw_call, diag)) goto fail;
                out->scalar_count = scope.count;
                memcpy(out->scalars, scope.scalars,
                       scope.count * sizeof(scope.scalars[0]));
                break;
            }
            case STMT_EMIT: {
                if (i + 1u != program->count) {
                    qn_diag_set_code(diag, "QN-E7589", stmt->line, stmt->column,
                                     "Step6 main emit must be final");
                    goto fail;
                }
                int id = find_scalar(out, stmt->as.emit.name);
                if (id < 0 || out->scalars[id].type != QIR_TYPE_U32) {
                    qn_diag_set_code(diag, "QN-E7504", stmt->line, stmt->column,
                                     "emit references unknown u32 scalar '%s'",
                                     stmt->as.emit.name);
                    goto fail;
                }
                ins.opcode = QIR_OP_U32_EMIT;
                ins.a = scalar_value(out, (uint16_t)id);
                if (!append_qir(out, ins, diag)) goto fail;
                emitted = true;
                break;
            }
            default:
                qn_diag_set_code(diag, "QN-E7590", stmt->line, stmt->column,
                                 "Step6 function programs permit only u32 let/arithmetic/call/emit in main");
                goto fail;
        }
    }

    if (!saw_call) {
        qn_diag_set_code(diag, "QN-E7591", 1, 1,
                         "Step6 main must contain at least one function call");
        goto fail;
    }
    if (!emitted || out->scalar_count == 0u) {
        qn_diag_set_code(diag, "QN-E7592", 1, 1,
                         "Step6 main must emit exactly one u32 result");
        goto fail;
    }
    out->capability_mask = QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    return QN_OK;

fail:
    qn_qir_free(out);
    return QN_ERR_SEMANTIC;
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
        case QIR_OP_JUMP_IF_FALSE: return "JUMP.IF.FALSE";
        case QIR_OP_JUMP: return "JUMP";
        case QIR_OP_U32_SET_ADD: return "U32.SET.ADD";
        case QIR_OP_U32_SET_SUB: return "U32.SET.SUB";
        case QIR_OP_U32_SET_MUL: return "U32.SET.MUL";
        case QIR_OP_U32_SET_DIV: return "U32.SET.DIV";
        case QIR_OP_REPEAT_ENTER: return "REPEAT.ENTER";
        case QIR_OP_REPEAT_NEXT: return "REPEAT.NEXT";
        case QIR_OP_CALL: return "CALL";
        case QIR_OP_RETURN: return "RETURN";
        default: return "INVALID";
    }
}

QNStatus qn_qir_build(const QNProgram *program,
                      const uint8_t source_digest[32],
                      QNQIRProgram *out,
                      QNDiagnostic *diag) {
    if (!program || !source_digest || !out || !diag) {
        if (diag) {
            qn_diag_set(diag, 0, 0, "invalid QIR build arguments");
        }
        return QN_ERR_RUNTIME;
    }
    {
        bool has_function_call = false;
        for (size_t i = 0u; i < program->count; ++i) {
            if (program->items[i].kind == STMT_CALL) {
                has_function_call = true;
                break;
            }
        }
        if (program->function_count > 0u || has_function_call) {
            return qn_qir_build_function_program(program, source_digest, out, diag);
        }
    }
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
    bool has_repeat_statement = false;
    bool has_if_statement = false;

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
            case STMT_IF:
                has_scalar_statement = true;
                has_if_statement = true;
                break;
            case STMT_REPEAT:
                has_scalar_statement = true;
                has_repeat_statement = true;
                break;
            default:
                break;
        }
    }

    if (has_repeat_statement && has_if_statement) {
        qn_diag_set_code(diag, "QN-E7555", 1, 1,
                         "Stage 7 Step 5 does not permit if/repeat mixing");
        goto fail;
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

            case STMT_REPEAT: {
                if (result_emitted) {
                    qn_diag_set_code(diag, "QN-E7563",
                                     stmt->line, stmt->column,
                                     "repeat cannot follow an emitted result");
                    goto fail;
                }
                if (stmt->as.repeat_stmt.iterations == 0u ||
                    stmt->as.repeat_stmt.iterations > QN_MAX_REPEAT_ITERATIONS) {
                    qn_diag_set_code(diag, "QN-E7550",
                                     stmt->line, stmt->column,
                                     "repeat count must be 1..%u",
                                     QN_MAX_REPEAT_ITERATIONS);
                    goto fail;
                }
                if (stmt->as.repeat_stmt.body_count == 0u) {
                    qn_diag_set_code(diag, "QN-E7553",
                                     stmt->line, stmt->column,
                                     "repeat body must contain at least one set statement");
                    goto fail;
                }
                if (i + 2u != program->count ||
                    program->items[i + 1u].kind != STMT_EMIT) {
                    qn_diag_set_code(diag, "QN-E7563",
                                     stmt->line, stmt->column,
                                     "bounded repeat must be followed by exactly one final emit");
                    goto fail;
                }

                QNQIRInstruction enter;
                memset(&enter, 0, sizeof(enter));
                enter.line = stmt->line;
                enter.column = stmt->column;
                enter.opcode = QIR_OP_REPEAT_ENTER;
                enter.a.register_id = (uint16_t)stmt->as.repeat_stmt.iterations;
                size_t enter_index = out->instruction_count;
                if (!append_qir(out, enter, diag)) goto fail;

                if (!compile_repeat_body(out,
                                         stmt->as.repeat_stmt.body_items,
                                         stmt->as.repeat_stmt.body_count,
                                         diag)) {
                    goto fail;
                }

                QNQIRInstruction next;
                memset(&next, 0, sizeof(next));
                next.line = stmt->line;
                next.column = stmt->column;
                next.opcode = QIR_OP_REPEAT_NEXT;
                next.imm = (uint32_t)enter_index;
                if (!append_qir(out, next, diag)) goto fail;

                if (out->instruction_count > UINT32_MAX) {
                    qn_diag_set_code(diag, "QN-E7564",
                                     stmt->line, stmt->column,
                                     "repeat exit target exceeds QBC address range");
                    goto fail;
                }
                out->instructions[enter_index].imm =
                    (uint32_t)out->instruction_count;
                out->capability_mask |= QN_CAP_COMPUTE_U32_SCALAR;
                break;
            }

            case STMT_IF: {
                if (result_emitted) {
                    qn_diag_set_code(diag, "QN-E7535",
                                     stmt->line, stmt->column,
                                     "if/else cannot follow an emitted result");
                    goto fail;
                }
                if (i + 1u != program->count) {
                    qn_diag_set_code(diag, "QN-E7535",
                                     stmt->line, stmt->column,
                                     "Stage 7 Step 4 if/else must terminate the scalar program");
                    goto fail;
                }

                int condition_id = find_scalar(
                    out, stmt->as.if_stmt.condition
                );
                if (condition_id < 0) {
                    qn_diag_set_code(diag, "QN-E7530",
                                     stmt->line, stmt->column,
                                     "if condition references unknown scalar '%s'",
                                     stmt->as.if_stmt.condition);
                    goto fail;
                }
                if (out->scalars[condition_id].type != QIR_TYPE_BOOL) {
                    qn_diag_set_code(diag, "QN-E7531",
                                     stmt->line, stmt->column,
                                     "if condition must be bool; got %s",
                                     qn_qir_type_name(
                                         out->scalars[condition_id].type
                                     ));
                    goto fail;
                }

                uint16_t outer_count = out->scalar_count;
                QNQIRInstruction branch;
                memset(&branch, 0, sizeof(branch));
                branch.line = stmt->line;
                branch.column = stmt->column;
                branch.opcode = QIR_OP_JUMP_IF_FALSE;
                branch.a = scalar_value(
                    out, (uint16_t)condition_id
                );
                size_t jump_if_index = out->instruction_count;
                if (!append_qir(out, branch, diag)) goto fail;

                QNQIRType then_type = QIR_TYPE_NONE;
                if (!compile_scalar_branch(
                        out,
                        stmt->as.if_stmt.then_items,
                        stmt->as.if_stmt.then_count,
                        outer_count,
                        &then_type,
                        diag)) {
                    goto fail;
                }

                memset(&branch, 0, sizeof(branch));
                branch.line = stmt->line;
                branch.column = stmt->column;
                branch.opcode = QIR_OP_JUMP;
                size_t jump_end_index = out->instruction_count;
                if (!append_qir(out, branch, diag)) goto fail;

                if (out->instruction_count > UINT32_MAX) {
                    qn_diag_set_code(diag, "QN-E7540",
                                     stmt->line, stmt->column,
                                     "control-flow target exceeds QBC address range");
                    goto fail;
                }
                out->instructions[jump_if_index].imm =
                    (uint32_t)out->instruction_count;

                QNQIRType else_type = QIR_TYPE_NONE;
                if (!compile_scalar_branch(
                        out,
                        stmt->as.if_stmt.else_items,
                        stmt->as.if_stmt.else_count,
                        outer_count,
                        &else_type,
                        diag)) {
                    goto fail;
                }

                if (then_type != else_type) {
                    qn_diag_set_code(diag, "QN-E7538",
                                     stmt->line, stmt->column,
                                     "if/else branch output types must match; got %s and %s",
                                     qn_qir_type_name(then_type),
                                     qn_qir_type_name(else_type));
                    goto fail;
                }
                if (out->instruction_count > UINT32_MAX) {
                    qn_diag_set_code(diag, "QN-E7540",
                                     stmt->line, stmt->column,
                                     "control-flow target exceeds QBC address range");
                    goto fail;
                }
                out->instructions[jump_end_index].imm =
                    (uint32_t)out->instruction_count;
                out->capability_mask |=
                    QN_CAP_COMPUTE_U32_SCALAR |
                    QN_CAP_EVIDENCE_EMIT;
                result_emitted = true;
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
    out->function_count = qir->function_count;
    out->main_entry_pc = qir->main_entry_instruction;
    for (uint16_t i = 0u; i < qir->function_count; ++i) {
        out->functions[i].entry_pc = qir->functions[i].entry_instruction;
        out->functions[i].end_pc = qir->functions[i].end_instruction;
        out->functions[i].scalar_count = qir->functions[i].scalar_count;
        out->functions[i].param_count = qir->functions[i].param_count;
        out->functions[i].flags = 0u;
    }
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
            case QIR_OP_JUMP_IF_FALSE:
                dst->opcode = OP_JUMP_IF_FALSE;
                dst->a = (uint8_t)src->a.register_id;
                dst->imm = src->imm;
                break;
            case QIR_OP_JUMP:
                dst->opcode = OP_JUMP;
                dst->imm = src->imm;
                break;
            case QIR_OP_U32_SET_ADD:
            case QIR_OP_U32_SET_SUB:
            case QIR_OP_U32_SET_MUL:
            case QIR_OP_U32_SET_DIV:
                dst->opcode = src->opcode == QIR_OP_U32_SET_ADD ? OP_U32_SET_ADD :
                              src->opcode == QIR_OP_U32_SET_SUB ? OP_U32_SET_SUB :
                              src->opcode == QIR_OP_U32_SET_MUL ? OP_U32_SET_MUL :
                                                                  OP_U32_SET_DIV;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = (uint8_t)src->a.register_id;
                dst->flags = (uint8_t)src->b.register_id;
                break;
            case QIR_OP_REPEAT_ENTER:
                dst->opcode = OP_REPEAT_ENTER;
                dst->a = (uint8_t)(src->a.register_id & 0xffu);
                dst->b = (uint8_t)(src->a.register_id >> 8);
                dst->imm = src->imm;
                break;
            case QIR_OP_REPEAT_NEXT:
                dst->opcode = OP_REPEAT_NEXT;
                dst->imm = src->imm;
                break;
            case QIR_OP_CALL:
                dst->opcode = OP_CALL;
                dst->a = (uint8_t)src->out.register_id;
                dst->b = src->a.type == QIR_TYPE_U32 ? (uint8_t)src->a.register_id : 0u;
                dst->flags = src->b.type == QIR_TYPE_U32 ? (uint8_t)src->b.register_id : 0u;
                dst->imm = src->imm;
                break;
            case QIR_OP_RETURN:
                dst->opcode = OP_RETURN;
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
    if (qn_qbc_has_functions(out)) {
        uint64_t step_bound = 0u;
        if (!qn_qbc_execution_step_bound(out, &step_bound) ||
            step_bound > QN_MAX_EXECUTION_STEPS) {
            qn_diag_set_code(diag, "QN-E7593", 0, 0,
                             "Step6 function execution budget exceeds %u steps",
                             QN_MAX_EXECUTION_STEPS);
            qn_bytecode_free(out);
            return QN_ERR_LIMIT;
        }
        if (!qn_qbc_is_typed_scalar_program(out)) {
            qn_diag_set_code(diag, "QN-E7594", 0, 0,
                             "Step6 function QBC contract invalid after lowering");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
    } else if (qn_qbc_has_bounded_repeat(out)) {
        uint64_t step_bound = 0u;
        if (!qn_qbc_execution_step_bound(out, &step_bound) ||
            step_bound > QN_MAX_EXECUTION_STEPS) {
            qn_diag_set_code(diag, "QN-E7565", 0, 0,
                             "bounded repeat execution budget exceeds %u steps",
                             QN_MAX_EXECUTION_STEPS);
            qn_bytecode_free(out);
            return QN_ERR_LIMIT;
        }
        if (!qn_qbc_is_typed_scalar_program(out)) {
            qn_diag_set_code(diag, "QN-E7566", 0, 0,
                             "bounded repeat QBC contract invalid after lowering");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
    }
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
    bool has_control_flow = false;
    bool has_bounded_repeat = false;
    for (size_t i = 0; i < qir->instruction_count; ++i) {
        if (qir->instructions[i].opcode == QIR_OP_REPEAT_ENTER ||
            qir->instructions[i].opcode == QIR_OP_REPEAT_NEXT) {
            has_bounded_repeat = true;
        }
        if (qir->instructions[i].opcode == QIR_OP_JUMP_IF_FALSE ||
            qir->instructions[i].opcode == QIR_OP_JUMP) {
            has_control_flow = true;
        }
    }
    if (qir->function_count > 0u) {
        fprintf(stream, "QBIT_NOVA_TYPED_QIR_V06_STEP6\n");
    } else if (has_bounded_repeat) {
        fprintf(stream, "QBIT_NOVA_TYPED_QIR_V05_STEP5\n");
    } else if (has_control_flow) {
        fprintf(stream, "QBIT_NOVA_TYPED_QIR_V04_STEP4\n");
    } else if (bool_count > 0u) {
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
    if (qir->function_count > 0u) {
        fprintf(stream, "functions=%u\n", qir->function_count);
        fprintf(stream, "main_entry=%u\n", qir->main_entry_instruction);
        for (uint16_t i = 0u; i < qir->function_count; ++i) {
            const QNQIRFunctionInfo *fn = &qir->functions[i];
            fprintf(stream, "FUNC %u name=%s entry=%u end=%u scalars=%u params=%u\n",
                    i, fn->name, fn->entry_instruction, fn->end_instruction,
                    fn->scalar_count, fn->param_count);
        }
    }

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
            case QIR_OP_JUMP_IF_FALSE:
                dump_value(qir, &ins->a, stream);
                fprintf(stream, " -> %u", ins->imm);
                break;
            case QIR_OP_JUMP:
                fprintf(stream, "-> %u", ins->imm);
                break;
            case QIR_OP_U32_SET_ADD:
            case QIR_OP_U32_SET_SUB:
            case QIR_OP_U32_SET_MUL:
            case QIR_OP_U32_SET_DIV:
                dump_value(qir, &ins->out, stream);
                fprintf(stream, " <- ");
                dump_value(qir, &ins->a, stream);
                fprintf(stream, ", ");
                dump_value(qir, &ins->b, stream);
                break;
            case QIR_OP_REPEAT_ENTER:
                fprintf(stream, "count=%u -> exit=%u",
                        ins->a.register_id, ins->imm);
                break;
            case QIR_OP_REPEAT_NEXT:
                fprintf(stream, "-> enter=%u", ins->imm);
                break;
            case QIR_OP_CALL:
                fprintf(stream, "fn=%u ", ins->imm);
                if (ins->a.type == QIR_TYPE_U32) dump_value(qir, &ins->a, stream);
                if (ins->b.type == QIR_TYPE_U32) { fprintf(stream, ", "); dump_value(qir, &ins->b, stream); }
                fprintf(stream, " -> ");
                dump_value(qir, &ins->out, stream);
                break;
            case QIR_OP_RETURN:
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
