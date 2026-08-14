#include "qn_qbc.h"
#include "qn_qir.h"

#include <stdlib.h>
#include <string.h>

#define QBC_V1_HEADER_SIZE 64u
#define QBC_V2_HEADER_SIZE 72u
#define QBC_V3_HEADER_SIZE 80u
#define QBC_V4_HEADER_SIZE 80u
#define QBC_V5_HEADER_SIZE 88u
#define QBC_V6_HEADER_SIZE 88u
#define QBC_V7_HEADER_SIZE 88u
#define QBC_V8_HEADER_SIZE 96u
#define QBC_V9_HEADER_SIZE 104u
#define QBC_REG_SIZE 68u
#define QBC_FUNC_SIZE 12u
#define QBC_INPUT_SIZE 36u
#define QBC_INSN_SIZE 8u

void qn_bytecode_free(QNBytecode *bc) {
    if (!bc) return;
    free(bc->instructions);
    memset(bc, 0, sizeof(*bc));
}

bool qn_qbc_is_bounded_u32_vector_add(const QNBytecode *bc) {
    if (!bc ||
        bc->total_qubits != 0u ||
        bc->register_count != 0u ||
        bc->scalar_count != 0u ||
        bc->scalar_bool_mask != 0u ||
        bc->function_count != 0u ||
        bc->input_count != 0u ||
        bc->initial_basis != 0u ||
        bc->default_shots != 1u ||
        bc->default_seed != 1u ||
        bc->instruction_count != 3u ||
        !bc->instructions) {
        return false;
    }

    QNCapabilityMask exact_capabilities =
        QN_CAP_COMPUTE_U32_ADD | QN_CAP_EVIDENCE_EMIT;

    if (bc->capability_mask != exact_capabilities) {
        return false;
    }

    const QNInstruction *compute = &bc->instructions[0];
    const QNInstruction *emit = &bc->instructions[1];
    const QNInstruction *end = &bc->instructions[2];

    return compute->opcode == OP_U32_VECTOR_ADD &&
           compute->a == 0u &&
           compute->b == 0u &&
           compute->flags == 0u &&
           compute->imm == QN_U32_VECTOR_ADD_COUNT &&
           emit->opcode == OP_EMIT &&
           emit->a == 0u &&
           emit->b == 0u &&
           emit->flags == 0u &&
           emit->imm == 0u &&
           end->opcode == OP_END &&
           end->a == 0u &&
           end->b == 0u &&
           end->flags == 0u &&
           end->imm == 0u;
}

static bool scalar_slot_is_bool(const QNBytecode *bc, uint16_t id) {
    return id < 64u &&
           (bc->scalar_bool_mask & (UINT64_C(1) << id)) != 0u;
}

static bool scalar_mask_fits_count(const QNBytecode *bc) {
    if (bc->scalar_count > QN_MAX_SCALARS) return false;
    if (bc->scalar_count >= 64u) return true;
    return (bc->scalar_bool_mask >> bc->scalar_count) == 0u;
}

bool qn_qbc_has_bounded_repeat(const QNBytecode *bc) {
    if (!bc || !bc->instructions) return false;
    for (size_t i = 0; i < bc->instruction_count; ++i) {
        if (bc->instructions[i].opcode == OP_REPEAT_ENTER ||
            bc->instructions[i].opcode == OP_REPEAT_NEXT) {
            return true;
        }
    }
    return false;
}

bool qn_qbc_has_functions(const QNBytecode *bc) {
    return bc && bc->function_count > 0u;
}

bool qn_qbc_has_runtime_inputs(const QNBytecode *bc) {
    return bc && bc->input_count > 0u;
}

bool qn_qbc_has_control_flow(const QNBytecode *bc) {
    if (!bc || !bc->instructions) return false;
    for (size_t i = 0; i < bc->instruction_count; ++i) {
        if (bc->instructions[i].opcode == OP_JUMP_IF_FALSE ||
            bc->instructions[i].opcode == OP_JUMP ||
            bc->instructions[i].opcode == OP_REPEAT_ENTER ||
            bc->instructions[i].opcode == OP_REPEAT_NEXT) {
            return true;
        }
    }
    return false;
}

static bool repeat_set_opcode(uint8_t opcode) {
    return opcode == OP_U32_SET_ADD ||
           opcode == OP_U32_SET_SUB ||
           opcode == OP_U32_SET_MUL ||
           opcode == OP_U32_SET_DIV;
}

static bool function_slot_initialized(uint64_t init, uint8_t slot) {
    return slot < 64u && (init & (UINT64_C(1) << slot)) != 0u;
}

static bool function_set_initialized(uint64_t *init, uint8_t slot) {
    if (!init || slot >= 64u) return false;
    uint64_t bit = UINT64_C(1) << slot;
    if ((*init & bit) != 0u) return false;
    *init |= bit;
    return true;
}

static bool function_range_validate(const QNBytecode *bc,
                                    uint16_t function_index,
                                    bool graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS]) {
    const QNFunctionRecord *fn = &bc->functions[function_index];
    if (fn->entry_pc >= fn->end_pc || fn->end_pc > bc->main_entry_pc ||
        fn->scalar_count == 0u || fn->scalar_count > QN_MAX_SCALARS ||
        fn->param_count > QN_MAX_FUNCTION_PARAMS ||
        fn->scalar_count < fn->param_count || fn->flags != 0u) {
        return false;
    }

    uint64_t init = fn->param_count == 0u
        ? 0u
        : ((UINT64_C(1) << fn->param_count) - UINT64_C(1));

    for (uint32_t pc = fn->entry_pc; pc < fn->end_pc; ++pc) {
        const QNInstruction *ins = &bc->instructions[pc];
        bool last = pc + 1u == fn->end_pc;
        switch (ins->opcode) {
            case OP_U32_CONST:
                if (last || ins->a >= fn->scalar_count ||
                    ins->b != 0u || ins->flags != 0u ||
                    !function_set_initialized(&init, ins->a)) return false;
                break;

            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
                if (last || ins->a >= fn->scalar_count ||
                    ins->b >= fn->scalar_count || ins->flags >= fn->scalar_count ||
                    ins->imm != 0u ||
                    !function_slot_initialized(init, ins->b) ||
                    !function_slot_initialized(init, ins->flags) ||
                    !function_set_initialized(&init, ins->a)) return false;
                break;

            case OP_CALL: {
                if (last || ins->imm >= bc->function_count ||
                    ins->a >= fn->scalar_count ||
                    !function_set_initialized(&init, ins->a)) return false;
                const QNFunctionRecord *callee = &bc->functions[ins->imm];
                if (callee->param_count == 0u) {
                    if (ins->b != 0u || ins->flags != 0u) return false;
                } else if (callee->param_count == 1u) {
                    if (ins->b >= fn->scalar_count || ins->flags != 0u ||
                        !function_slot_initialized(init, ins->b)) return false;
                } else if (callee->param_count == 2u) {
                    if (ins->b >= fn->scalar_count || ins->flags >= fn->scalar_count ||
                        !function_slot_initialized(init, ins->b) ||
                        !function_slot_initialized(init, ins->flags)) return false;
                } else {
                    return false;
                }
                graph[function_index][ins->imm] = true;
                break;
            }

            case OP_RETURN:
                if (!last || ins->a >= fn->scalar_count ||
                    !function_slot_initialized(init, ins->a) ||
                    ins->b != 0u || ins->flags != 0u || ins->imm != 0u) {
                    return false;
                }
                break;

            default:
                return false;
        }
    }
    return bc->instructions[fn->end_pc - 1u].opcode == OP_RETURN;
}

static bool function_depth_dfs(uint16_t node,
                               uint16_t function_count,
                               bool graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS],
                               uint8_t state[QN_MAX_FUNCTIONS],
                               uint8_t memo[QN_MAX_FUNCTIONS],
                               uint8_t *depth_out) {
    if (state[node] == 1u) return false;
    if (state[node] == 2u) {
        *depth_out = memo[node];
        return true;
    }
    state[node] = 1u;
    uint8_t depth = 1u;
    for (uint16_t callee = 0u; callee < function_count; ++callee) {
        if (!graph[node][callee]) continue;
        uint8_t child = 0u;
        if (!function_depth_dfs(callee, function_count, graph,
                                state, memo, &child)) return false;
        if (child >= UINT8_MAX) return false;
        uint8_t candidate = (uint8_t)(child + 1u);
        if (candidate > depth) depth = candidate;
    }
    state[node] = 2u;
    memo[node] = depth;
    *depth_out = depth;
    return true;
}

static bool function_cost_dfs(const QNBytecode *bc,
                              uint16_t node,
                              uint64_t memo[QN_MAX_FUNCTIONS],
                              bool done[QN_MAX_FUNCTIONS],
                              uint64_t *cost_out) {
    if (done[node]) {
        *cost_out = memo[node];
        return true;
    }
    const QNFunctionRecord *fn = &bc->functions[node];
    uint64_t cost = (uint64_t)(fn->end_pc - fn->entry_pc);
    for (uint32_t pc = fn->entry_pc; pc < fn->end_pc; ++pc) {
        const QNInstruction *ins = &bc->instructions[pc];
        if (ins->opcode != OP_CALL) continue;
        uint64_t child = 0u;
        if (ins->imm >= bc->function_count ||
            !function_cost_dfs(bc, (uint16_t)ins->imm, memo, done, &child) ||
            cost > UINT64_MAX - child) return false;
        cost += child;
    }
    done[node] = true;
    memo[node] = cost;
    *cost_out = cost;
    return true;
}

static bool qn_qbc_is_function_contract(const QNBytecode *bc,
                                        uint64_t *steps_out) {
    if (!bc || !bc->instructions ||
        bc->total_qubits != 0u || bc->register_count != 0u ||
        bc->function_count == 0u || bc->function_count > QN_MAX_FUNCTIONS ||
        bc->scalar_count == 0u || bc->scalar_count > QN_MAX_SCALARS ||
        bc->scalar_bool_mask != 0u ||
        bc->initial_basis != 0u || bc->default_shots != 1u ||
        bc->default_seed != 1u ||
        bc->instruction_count < 4u ||
        bc->instruction_count > QN_MAX_INSTRUCTIONS ||
        bc->main_entry_pc >= bc->instruction_count - 1u) return false;

    QNCapabilityMask exact =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    if (bc->capability_mask != exact) return false;

    bool graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS] = {{false}};
    uint32_t expected_entry = 0u;
    for (uint16_t i = 0u; i < bc->function_count; ++i) {
        const QNFunctionRecord *fn = &bc->functions[i];
        if (fn->entry_pc != expected_entry ||
            !function_range_validate(bc, i, graph)) return false;
        expected_entry = fn->end_pc;
    }
    if (expected_entry != bc->main_entry_pc) return false;

    uint8_t state[QN_MAX_FUNCTIONS] = {0};
    uint8_t depth_memo[QN_MAX_FUNCTIONS] = {0};
    for (uint16_t i = 0u; i < bc->function_count; ++i) {
        uint8_t depth = 0u;
        if (!function_depth_dfs(i, bc->function_count, graph,
                                state, depth_memo, &depth) ||
            depth > QN_MAX_CALL_DEPTH) return false;
    }

    uint64_t init = 0u;
    bool saw_call = false;
    bool emitted = false;
    for (uint32_t pc = bc->main_entry_pc; pc < bc->instruction_count; ++pc) {
        const QNInstruction *ins = &bc->instructions[pc];
        bool last = pc + 1u == bc->instruction_count;
        if (last) {
            if (ins->opcode != OP_END || ins->a != 0u || ins->b != 0u ||
                ins->flags != 0u || ins->imm != 0u || !emitted || !saw_call) {
                return false;
            }
            break;
        }
        if (emitted) return false;
        switch (ins->opcode) {
            case OP_U32_CONST:
                if (ins->a >= bc->scalar_count || ins->b != 0u ||
                    ins->flags != 0u ||
                    !function_set_initialized(&init, ins->a)) return false;
                break;
            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
                if (ins->a >= bc->scalar_count || ins->b >= bc->scalar_count ||
                    ins->flags >= bc->scalar_count || ins->imm != 0u ||
                    !function_slot_initialized(init, ins->b) ||
                    !function_slot_initialized(init, ins->flags) ||
                    !function_set_initialized(&init, ins->a)) return false;
                break;
            case OP_CALL: {
                if (ins->imm >= bc->function_count || ins->a >= bc->scalar_count ||
                    !function_set_initialized(&init, ins->a)) return false;
                const QNFunctionRecord *callee = &bc->functions[ins->imm];
                if (callee->param_count == 0u) {
                    if (ins->b != 0u || ins->flags != 0u) return false;
                } else if (callee->param_count == 1u) {
                    if (ins->b >= bc->scalar_count || ins->flags != 0u ||
                        !function_slot_initialized(init, ins->b)) return false;
                } else if (callee->param_count == 2u) {
                    if (ins->b >= bc->scalar_count || ins->flags >= bc->scalar_count ||
                        !function_slot_initialized(init, ins->b) ||
                        !function_slot_initialized(init, ins->flags)) return false;
                } else {
                    return false;
                }
                saw_call = true;
                break;
            }
            case OP_U32_EMIT:
                if (ins->a >= bc->scalar_count ||
                    !function_slot_initialized(init, ins->a) ||
                    ins->b != 0u || ins->flags != 0u || ins->imm != 0u) {
                    return false;
                }
                emitted = true;
                break;
            default:
                return false;
        }
    }

    uint64_t cost_memo[QN_MAX_FUNCTIONS] = {0};
    bool cost_done[QN_MAX_FUNCTIONS] = {false};
    uint64_t steps = (uint64_t)(bc->instruction_count - bc->main_entry_pc);
    for (uint32_t pc = bc->main_entry_pc; pc < bc->instruction_count; ++pc) {
        const QNInstruction *ins = &bc->instructions[pc];
        if (ins->opcode != OP_CALL) continue;
        uint64_t child = 0u;
        if (!function_cost_dfs(bc, (uint16_t)ins->imm,
                               cost_memo, cost_done, &child) ||
            steps > UINT64_MAX - child) return false;
        steps += child;
    }
    if (steps > QN_MAX_EXECUTION_STEPS) return false;
    if (steps_out) *steps_out = steps;
    return true;
}


static bool qn_input_digest_equal(const uint8_t a[32], const uint8_t b[32]) {
    return memcmp(a, b, 32) == 0;
}

static bool qn_qbc_is_runtime_input_contract(const QNBytecode *bc,
                                             uint64_t *steps_out) {
    if (!bc || !bc->instructions ||
        bc->total_qubits != 0u || bc->register_count != 0u ||
        bc->input_count == 0u || bc->input_count > QN_MAX_RUNTIME_INPUTS ||
        bc->input_abi_version != QN_RUNTIME_INPUT_ABI_V1 ||
        bc->function_count > QN_MAX_FUNCTIONS ||
        bc->scalar_count < bc->input_count ||
        bc->scalar_count > QN_MAX_SCALARS ||
        bc->scalar_bool_mask != 0u ||
        bc->initial_basis != 0u || bc->default_shots != 1u ||
        bc->default_seed != 1u ||
        bc->instruction_count < 2u ||
        bc->instruction_count > QN_MAX_INSTRUCTIONS ||
        bc->main_entry_pc >= bc->instruction_count - 1u) {
        return false;
    }

    QNCapabilityMask exact =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    if (bc->capability_mask != exact) return false;

    for (uint16_t i = 0u; i < bc->input_count; ++i) {
        const QNInputRecord *input = &bc->inputs[i];
        if (input->main_scalar_slot != i ||
            input->main_scalar_slot >= bc->scalar_count ||
            input->type != QN_RUNTIME_INPUT_TYPE_U32 ||
            input->flags != 0u) {
            return false;
        }
        for (uint16_t j = 0u; j < i; ++j) {
            if (qn_input_digest_equal(input->input_name_sha256,
                                      bc->inputs[j].input_name_sha256)) {
                return false;
            }
        }
    }

    bool graph[QN_MAX_FUNCTIONS][QN_MAX_FUNCTIONS] = {{false}};
    uint32_t expected_entry = 0u;
    for (uint16_t i = 0u; i < bc->function_count; ++i) {
        const QNFunctionRecord *fn = &bc->functions[i];
        if (fn->entry_pc != expected_entry ||
            !function_range_validate(bc, i, graph)) {
            return false;
        }
        expected_entry = fn->end_pc;
    }

    if (bc->function_count == 0u) {
        if (bc->main_entry_pc != 0u) return false;
    } else if (expected_entry != bc->main_entry_pc) {
        return false;
    }

    uint8_t state[QN_MAX_FUNCTIONS] = {0};
    uint8_t depth_memo[QN_MAX_FUNCTIONS] = {0};
    for (uint16_t i = 0u; i < bc->function_count; ++i) {
        uint8_t depth = 0u;
        if (!function_depth_dfs(i, bc->function_count, graph,
                                state, depth_memo, &depth) ||
            depth > QN_MAX_CALL_DEPTH) {
            return false;
        }
    }

    uint64_t init = 0u;
    for (uint16_t i = 0u; i < bc->input_count; ++i) {
        init |= UINT64_C(1) << i;
    }

    bool emitted = false;
    for (uint32_t pc = bc->main_entry_pc; pc < bc->instruction_count; ++pc) {
        const QNInstruction *ins = &bc->instructions[pc];
        bool last = pc + 1u == bc->instruction_count;

        if (last) {
            if (ins->opcode != OP_END || ins->a != 0u || ins->b != 0u ||
                ins->flags != 0u || ins->imm != 0u || !emitted) {
                return false;
            }
            break;
        }

        if (emitted) return false;

        switch (ins->opcode) {
            case OP_U32_CONST:
                if (ins->a >= bc->scalar_count || ins->b != 0u ||
                    ins->flags != 0u ||
                    !function_set_initialized(&init, ins->a)) {
                    return false;
                }
                break;

            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
                if (ins->a >= bc->scalar_count ||
                    ins->b >= bc->scalar_count ||
                    ins->flags >= bc->scalar_count ||
                    ins->imm != 0u ||
                    !function_slot_initialized(init, ins->b) ||
                    !function_slot_initialized(init, ins->flags) ||
                    !function_set_initialized(&init, ins->a)) {
                    return false;
                }
                break;

            case OP_CALL: {
                if (ins->imm >= bc->function_count ||
                    ins->a >= bc->scalar_count ||
                    !function_set_initialized(&init, ins->a)) {
                    return false;
                }
                const QNFunctionRecord *callee = &bc->functions[ins->imm];
                if (callee->param_count == 0u) {
                    if (ins->b != 0u || ins->flags != 0u) return false;
                } else if (callee->param_count == 1u) {
                    if (ins->b >= bc->scalar_count || ins->flags != 0u ||
                        !function_slot_initialized(init, ins->b)) {
                        return false;
                    }
                } else if (callee->param_count == 2u) {
                    if (ins->b >= bc->scalar_count ||
                        ins->flags >= bc->scalar_count ||
                        !function_slot_initialized(init, ins->b) ||
                        !function_slot_initialized(init, ins->flags)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            }

            case OP_U32_EMIT:
                if (ins->a >= bc->scalar_count ||
                    !function_slot_initialized(init, ins->a) ||
                    ins->b != 0u || ins->flags != 0u || ins->imm != 0u) {
                    return false;
                }
                emitted = true;
                break;

            default:
                return false;
        }
    }

    uint64_t cost_memo[QN_MAX_FUNCTIONS] = {0};
    bool cost_done[QN_MAX_FUNCTIONS] = {false};
    uint64_t steps =
        (uint64_t)(bc->instruction_count - bc->main_entry_pc);

    for (uint32_t pc = bc->main_entry_pc; pc < bc->instruction_count; ++pc) {
        const QNInstruction *ins = &bc->instructions[pc];
        if (ins->opcode != OP_CALL) continue;
        uint64_t child = 0u;
        if (!function_cost_dfs(bc, (uint16_t)ins->imm,
                               cost_memo, cost_done, &child) ||
            steps > UINT64_MAX - child) {
            return false;
        }
        steps += child;
    }

    if (steps > QN_MAX_EXECUTION_STEPS) return false;
    if (steps_out) *steps_out = steps;
    return true;
}

bool qn_qbc_execution_step_bound(const QNBytecode *bc, uint64_t *steps_out) {
    if (!bc || !bc->instructions || bc->instruction_count == 0u ||
        !steps_out) return false;

    if (qn_qbc_has_runtime_inputs(bc)) {
        return qn_qbc_is_runtime_input_contract(bc, steps_out);
    }

    if (qn_qbc_has_functions(bc)) {
        return qn_qbc_is_function_contract(bc, steps_out);
    }

    if (!qn_qbc_has_bounded_repeat(bc)) {
        *steps_out = (uint64_t)bc->instruction_count;
        return *steps_out <= QN_MAX_EXECUTION_STEPS;
    }

    size_t enter = SIZE_MAX;
    size_t next = SIZE_MAX;
    uint32_t count = 0u;
    for (size_t i = 0; i < bc->instruction_count; ++i) {
        const QNInstruction *ins = &bc->instructions[i];
        if (ins->opcode == OP_REPEAT_ENTER) {
            if (enter != SIZE_MAX) return false;
            enter = i;
            count = (uint32_t)ins->a | ((uint32_t)ins->b << 8);
        } else if (ins->opcode == OP_REPEAT_NEXT) {
            if (next != SIZE_MAX) return false;
            next = i;
        }
    }
    if (enter == SIZE_MAX || next == SIZE_MAX ||
        count == 0u || count > QN_MAX_REPEAT_ITERATIONS ||
        next <= enter + 1u ||
        bc->instructions[enter].imm != next + 1u ||
        bc->instructions[next].imm != enter) {
        return false;
    }

    uint64_t body = (uint64_t)(next - enter - 1u);
    uint64_t before = (uint64_t)enter;
    uint64_t after = (uint64_t)(bc->instruction_count - (next + 1u));
    uint64_t per_iteration = body + UINT64_C(2);
    if (per_iteration > UINT64_MAX / count) return false;
    uint64_t loop_steps = per_iteration * count;
    if (before > UINT64_MAX - loop_steps ||
        before + loop_steps > UINT64_MAX - after) return false;
    *steps_out = before + loop_steps + after;
    return *steps_out <= QN_MAX_EXECUTION_STEPS;
}

static bool qn_qbc_is_bounded_repeat_contract(const QNBytecode *bc) {
    if (!bc || !bc->instructions ||
        bc->total_qubits != 0u || bc->register_count != 0u ||
        bc->scalar_count == 0u || bc->scalar_count > QN_MAX_SCALARS ||
        !scalar_mask_fits_count(bc) ||
        bc->initial_basis != 0u || bc->default_shots != 1u ||
        bc->default_seed != 1u || bc->instruction_count < 6u ||
        bc->instruction_count > QN_MAX_INSTRUCTIONS) return false;

    QNCapabilityMask exact =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    if (bc->capability_mask != exact) return false;

    size_t enter = SIZE_MAX;
    size_t next = SIZE_MAX;
    uint32_t repeat_count = 0u;
    for (size_t i = 0; i < bc->instruction_count; ++i) {
        uint8_t opcode = bc->instructions[i].opcode;
        if (opcode == OP_REPEAT_ENTER) {
            if (enter != SIZE_MAX) return false;
            enter = i;
            repeat_count = (uint32_t)bc->instructions[i].a |
                           ((uint32_t)bc->instructions[i].b << 8);
        } else if (opcode == OP_REPEAT_NEXT) {
            if (next != SIZE_MAX) return false;
            next = i;
        } else if (opcode == OP_JUMP_IF_FALSE || opcode == OP_JUMP) {
            return false;
        }
    }
    if (enter == SIZE_MAX || next == SIZE_MAX ||
        repeat_count == 0u || repeat_count > QN_MAX_REPEAT_ITERATIONS ||
        next <= enter + 1u ||
        bc->instructions[enter].flags != 0u ||
        bc->instructions[enter].imm != next + 1u ||
        bc->instructions[next].a != 0u ||
        bc->instructions[next].b != 0u ||
        bc->instructions[next].flags != 0u ||
        bc->instructions[next].imm != enter ||
        next + 3u != bc->instruction_count) return false;

    uint64_t init = 0u;
    uint64_t ever_initialized = 0u;
    bool saw_compute = false;

    for (size_t i = 0; i < enter; ++i) {
        const QNInstruction *ins = &bc->instructions[i];
        uint64_t bit_a = ins->a < 64u ? UINT64_C(1) << ins->a : 0u;
        uint64_t bit_b = ins->b < 64u ? UINT64_C(1) << ins->b : 0u;
        uint64_t bit_f = ins->flags < 64u ? UINT64_C(1) << ins->flags : 0u;
        switch (ins->opcode) {
            case OP_U32_CONST:
                if (ins->a >= bc->scalar_count || scalar_slot_is_bool(bc, ins->a) ||
                    (init & bit_a) != 0u || ins->b != 0u || ins->flags != 0u)
                    return false;
                init |= bit_a; ever_initialized |= bit_a; saw_compute = true;
                break;
            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
                if (ins->a >= bc->scalar_count || ins->b >= bc->scalar_count ||
                    ins->flags >= bc->scalar_count ||
                    scalar_slot_is_bool(bc, ins->a) ||
                    scalar_slot_is_bool(bc, ins->b) ||
                    scalar_slot_is_bool(bc, ins->flags) ||
                    (init & bit_a) != 0u || (init & bit_b) == 0u ||
                    (init & bit_f) == 0u || ins->imm != 0u) return false;
                init |= bit_a; ever_initialized |= bit_a; saw_compute = true;
                break;
            case OP_U32_EQ:
            case OP_U32_NE:
            case OP_U32_LT:
            case OP_U32_LE:
            case OP_U32_GT:
            case OP_U32_GE:
                if (ins->a >= bc->scalar_count || ins->b >= bc->scalar_count ||
                    ins->flags >= bc->scalar_count ||
                    !scalar_slot_is_bool(bc, ins->a) ||
                    scalar_slot_is_bool(bc, ins->b) ||
                    scalar_slot_is_bool(bc, ins->flags) ||
                    (init & bit_a) != 0u || (init & bit_b) == 0u ||
                    (init & bit_f) == 0u || ins->imm != 0u) return false;
                init |= bit_a; ever_initialized |= bit_a; saw_compute = true;
                break;
            default:
                return false;
        }
    }

    uint64_t required = bc->scalar_count == 64u
        ? UINT64_MAX
        : ((UINT64_C(1) << bc->scalar_count) - UINT64_C(1));
    if ((ever_initialized & required) != required) return false;

    for (size_t i = enter + 1u; i < next; ++i) {
        const QNInstruction *ins = &bc->instructions[i];
        if (!repeat_set_opcode(ins->opcode) ||
            ins->a >= bc->scalar_count || ins->b >= bc->scalar_count ||
            ins->flags >= bc->scalar_count || ins->imm != 0u ||
            scalar_slot_is_bool(bc, ins->a) ||
            scalar_slot_is_bool(bc, ins->b) ||
            scalar_slot_is_bool(bc, ins->flags)) return false;
        uint64_t bit_a = UINT64_C(1) << ins->a;
        uint64_t bit_b = UINT64_C(1) << ins->b;
        uint64_t bit_f = UINT64_C(1) << ins->flags;
        if ((init & bit_a) == 0u || (init & bit_b) == 0u ||
            (init & bit_f) == 0u) return false;
        saw_compute = true;
    }

    const QNInstruction *emit = &bc->instructions[next + 1u];
    uint64_t emit_bit = emit->a < 64u ? UINT64_C(1) << emit->a : 0u;
    if (emit->a >= bc->scalar_count || (init & emit_bit) == 0u ||
        emit->b != 0u || emit->flags != 0u || emit->imm != 0u) return false;
    if (emit->opcode == OP_U32_EMIT) {
        if (scalar_slot_is_bool(bc, emit->a)) return false;
    } else if (emit->opcode == OP_BOOL_EMIT) {
        if (!scalar_slot_is_bool(bc, emit->a)) return false;
    } else {
        return false;
    }

    const QNInstruction *end = &bc->instructions[next + 2u];
    if (end->opcode != OP_END || end->a != 0u || end->b != 0u ||
        end->flags != 0u || end->imm != 0u || !saw_compute) return false;

    uint64_t steps = 0u;
    return qn_qbc_execution_step_bound(bc, &steps) &&
           steps <= QN_MAX_EXECUTION_STEPS;
}

static void scalar_propagate(size_t target,
                             uint64_t init_mask,
                             uint8_t emit_state,
                             uint8_t output_types,
                             bool *reachable,
                             uint64_t *init_in,
                             uint8_t *emit_in,
                             uint8_t *output_in) {
    if (!reachable[target]) {
        reachable[target] = true;
        init_in[target] = init_mask;
        emit_in[target] = emit_state;
        output_in[target] = output_types;
        return;
    }
    init_in[target] &= init_mask;
    emit_in[target] |= emit_state;
    output_in[target] |= output_types;
}

bool qn_qbc_is_typed_scalar_program(const QNBytecode *bc) {
    enum {
        EMIT_NONE = 1u,
        EMIT_DONE = 2u,
        OUTPUT_U32 = 1u,
        OUTPUT_BOOL = 2u
    };

    if (!bc ||
        bc->total_qubits != 0u ||
        bc->register_count != 0u ||
        bc->scalar_count == 0u ||
        bc->scalar_count > QN_MAX_SCALARS ||
        !scalar_mask_fits_count(bc) ||
        bc->initial_basis != 0u ||
        bc->default_shots != 1u ||
        bc->default_seed != 1u ||
        bc->instruction_count <
            (qn_qbc_has_runtime_inputs(bc) ? 2u : 3u) ||
        bc->instruction_count > QN_MAX_INSTRUCTIONS ||
        !bc->instructions) {
        return false;
    }

    QNCapabilityMask exact =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    if (bc->capability_mask != exact) return false;

    if (qn_qbc_has_runtime_inputs(bc)) {
        return qn_qbc_is_runtime_input_contract(bc, NULL);
    }

    if (qn_qbc_has_functions(bc)) {
        return qn_qbc_is_function_contract(bc, NULL);
    }

    if (qn_qbc_has_bounded_repeat(bc)) {
        return qn_qbc_is_bounded_repeat_contract(bc);
    }

    bool *reachable = calloc(bc->instruction_count, sizeof(*reachable));
    uint64_t *init_in = calloc(bc->instruction_count, sizeof(*init_in));
    uint8_t *emit_in = calloc(bc->instruction_count, sizeof(*emit_in));
    uint8_t *output_in = calloc(bc->instruction_count, sizeof(*output_in));
    if (!reachable || !init_in || !emit_in || !output_in) {
        free(reachable);
        free(init_in);
        free(emit_in);
        free(output_in);
        return false;
    }

    reachable[0] = true;
    emit_in[0] = EMIT_NONE;
    uint64_t ever_initialized = 0u;
    bool saw_compute = false;
    bool valid = false;

    for (size_t i = 0; i < bc->instruction_count; ++i) {
        const QNInstruction *ins = &bc->instructions[i];
        bool last = i + 1u == bc->instruction_count;
        if (!reachable[i]) goto done;

        uint64_t init = init_in[i];
        uint8_t emit_state = emit_in[i];
        uint8_t output_types = output_in[i];
        uint64_t bit_a = ins->a < 64u
            ? (UINT64_C(1) << ins->a) : 0u;
        uint64_t bit_b = ins->b < 64u
            ? (UINT64_C(1) << ins->b) : 0u;
        uint64_t bit_flags = ins->flags < 64u
            ? (UINT64_C(1) << ins->flags) : 0u;

        if (last) {
            if (ins->opcode != OP_END ||
                ins->a != 0u || ins->b != 0u ||
                ins->flags != 0u || ins->imm != 0u ||
                emit_state != EMIT_DONE ||
                (output_types != OUTPUT_U32 &&
                 output_types != OUTPUT_BOOL) ||
                !saw_compute) {
                goto done;
            }
            uint64_t required = bc->scalar_count == 64u
                ? UINT64_MAX
                : ((UINT64_C(1) << bc->scalar_count) - UINT64_C(1));
            if ((ever_initialized & required) != required) goto done;
            valid = true;
            goto done;
        }

        switch (ins->opcode) {
            case OP_U32_CONST:
                if (ins->a >= bc->scalar_count ||
                    scalar_slot_is_bool(bc, ins->a) ||
                    (init & bit_a) != 0u ||
                    ins->b != 0u || ins->flags != 0u) {
                    goto done;
                }
                init |= bit_a;
                ever_initialized |= bit_a;
                saw_compute = true;
                scalar_propagate(i + 1u, init, emit_state, output_types,
                                 reachable, init_in, emit_in, output_in);
                break;

            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
                if (ins->a >= bc->scalar_count ||
                    ins->b >= bc->scalar_count ||
                    ins->flags >= bc->scalar_count ||
                    scalar_slot_is_bool(bc, ins->a) ||
                    scalar_slot_is_bool(bc, ins->b) ||
                    scalar_slot_is_bool(bc, ins->flags) ||
                    (init & bit_a) != 0u ||
                    (init & bit_b) == 0u ||
                    (init & bit_flags) == 0u ||
                    ins->imm != 0u) {
                    goto done;
                }
                init |= bit_a;
                ever_initialized |= bit_a;
                saw_compute = true;
                scalar_propagate(i + 1u, init, emit_state, output_types,
                                 reachable, init_in, emit_in, output_in);
                break;

            case OP_U32_EQ:
            case OP_U32_NE:
            case OP_U32_LT:
            case OP_U32_LE:
            case OP_U32_GT:
            case OP_U32_GE:
                if (ins->a >= bc->scalar_count ||
                    ins->b >= bc->scalar_count ||
                    ins->flags >= bc->scalar_count ||
                    !scalar_slot_is_bool(bc, ins->a) ||
                    scalar_slot_is_bool(bc, ins->b) ||
                    scalar_slot_is_bool(bc, ins->flags) ||
                    (init & bit_a) != 0u ||
                    (init & bit_b) == 0u ||
                    (init & bit_flags) == 0u ||
                    ins->imm != 0u) {
                    goto done;
                }
                init |= bit_a;
                ever_initialized |= bit_a;
                saw_compute = true;
                scalar_propagate(i + 1u, init, emit_state, output_types,
                                 reachable, init_in, emit_in, output_in);
                break;

            case OP_U32_EMIT:
                if (ins->a >= bc->scalar_count ||
                    scalar_slot_is_bool(bc, ins->a) ||
                    (init & bit_a) == 0u ||
                    emit_state != EMIT_NONE ||
                    ins->b != 0u || ins->flags != 0u ||
                    ins->imm != 0u) {
                    goto done;
                }
                scalar_propagate(i + 1u, init, EMIT_DONE, OUTPUT_U32,
                                 reachable, init_in, emit_in, output_in);
                break;

            case OP_BOOL_EMIT:
                if (ins->a >= bc->scalar_count ||
                    !scalar_slot_is_bool(bc, ins->a) ||
                    (init & bit_a) == 0u ||
                    emit_state != EMIT_NONE ||
                    ins->b != 0u || ins->flags != 0u ||
                    ins->imm != 0u) {
                    goto done;
                }
                scalar_propagate(i + 1u, init, EMIT_DONE, OUTPUT_BOOL,
                                 reachable, init_in, emit_in, output_in);
                break;

            case OP_JUMP_IF_FALSE:
                if (ins->a >= bc->scalar_count ||
                    !scalar_slot_is_bool(bc, ins->a) ||
                    (init & bit_a) == 0u ||
                    ins->b != 0u || ins->flags != 0u ||
                    ins->imm <= i ||
                    ins->imm >= bc->instruction_count) {
                    goto done;
                }
                scalar_propagate(i + 1u, init, emit_state, output_types,
                                 reachable, init_in, emit_in, output_in);
                scalar_propagate(ins->imm, init, emit_state, output_types,
                                 reachable, init_in, emit_in, output_in);
                break;

            case OP_JUMP:
                if (ins->a != 0u || ins->b != 0u ||
                    ins->flags != 0u ||
                    ins->imm <= i ||
                    ins->imm >= bc->instruction_count) {
                    goto done;
                }
                scalar_propagate(ins->imm, init, emit_state, output_types,
                                 reachable, init_in, emit_in, output_in);
                break;

            default:
                goto done;
        }
    }

done:
    free(reachable);
    free(init_in);
    free(emit_in);
    free(output_in);
    return valid;
}

bool qn_qbc_is_u32_scalar_program(const QNBytecode *bc) {
    return bc && bc->function_count == 0u &&
           bc->input_count == 0u &&
           bc->scalar_bool_mask == 0u &&
           qn_qbc_is_typed_scalar_program(bc);
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
    bool has_runtime_inputs = qn_qbc_has_runtime_inputs(bc);
    bool has_functions = qn_qbc_has_functions(bc);
    bool has_bounded_repeat = qn_qbc_has_bounded_repeat(bc);
    bool has_control_flow = qn_qbc_has_control_flow(bc);
    uint16_t version = has_runtime_inputs
        ? 9u
        : (has_functions
            ? 8u
            : (has_bounded_repeat
                ? 7u
                : (has_control_flow
                    ? 6u
                    : (bc->scalar_bool_mask != 0u
                        ? 5u
                        : (bc->scalar_count > 0u ? 4u : 3u)))));
    uint16_t header_size = version == 9u
        ? QBC_V9_HEADER_SIZE
        : (version == 8u
            ? QBC_V8_HEADER_SIZE
            : (version >= 5u
                ? QBC_V5_HEADER_SIZE
                : (version == 4u ? QBC_V4_HEADER_SIZE : QBC_V3_HEADER_SIZE)));
    size_t size =
        header_size +
        bc->register_count * QBC_REG_SIZE +
        (size_t)bc->function_count * QBC_FUNC_SIZE +
        (size_t)bc->input_count * QBC_INPUT_SIZE +
        bc->instruction_count * QBC_INSN_SIZE;

    uint8_t *data = calloc(size, 1);
    if (!data) {
        qn_diag_set(diag, 0, 0, "out of memory encoding QBC");
        return QN_ERR_QBC;
    }

    memcpy(data, "QBCN", 4);
    put16(data + 4, version);
    put16(data + 6, header_size);
    put32(data + 8, (uint32_t)bc->instruction_count);
    put16(data + 12, bc->total_qubits);
    put16(data + 14, bc->register_count);
    put64(data + 16, bc->initial_basis);
    put32(data + 24, bc->default_shots);
    put64(data + 28, bc->default_seed);
    memcpy(data + 36, bc->source_digest, 32);
    put64(data + 68, bc->capability_mask);
    if (version >= 4u) put16(data + 76, bc->scalar_count);
    if (version >= 5u) put64(data + 80, bc->scalar_bool_mask);
    if (version == 8u || version == 9u) {
        put16(data + 88, bc->function_count);
        put16(data + 90, QBC_FUNC_SIZE);
        put32(data + 92, bc->main_entry_pc);
    }
    if (version == 9u) {
        put16(data + 96, bc->input_count);
        put16(data + 98, QBC_INPUT_SIZE);
        put16(data + 100, bc->input_abi_version);
        put16(data + 102, 0u);
    }

    size_t at = header_size;

    for (uint16_t i = 0; i < bc->register_count; ++i) {
        memcpy(data + at, bc->registers[i].name, QN_NAME_CAP);
        put16(data + at + 64, bc->registers[i].base);
        put16(data + at + 66, bc->registers[i].width);
        at += QBC_REG_SIZE;
    }

    for (uint16_t i = 0u; i < bc->function_count; ++i) {
        const QNFunctionRecord *fn = &bc->functions[i];
        put32(data + at, fn->entry_pc);
        put32(data + at + 4, fn->end_pc);
        put16(data + at + 8, fn->scalar_count);
        data[at + 10] = fn->param_count;
        data[at + 11] = fn->flags;
        at += QBC_FUNC_SIZE;
    }

    for (uint16_t i = 0u; i < bc->input_count; ++i) {
        const QNInputRecord *input = &bc->inputs[i];
        memcpy(data + at, input->input_name_sha256, 32);
        put16(data + at + 32, input->main_scalar_slot);
        data[at + 34] = input->type;
        data[at + 35] = input->flags;
        at += QBC_INPUT_SIZE;
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
          (version == 3u && header_size == QBC_V3_HEADER_SIZE) ||
          (version == 4u && header_size == QBC_V4_HEADER_SIZE) ||
          (version == 5u && header_size == QBC_V5_HEADER_SIZE) ||
          (version == 6u && header_size == QBC_V6_HEADER_SIZE) ||
          (version == 7u && header_size == QBC_V7_HEADER_SIZE) ||
          (version == 8u && header_size == QBC_V8_HEADER_SIZE) ||
          (version == 9u && header_size == QBC_V9_HEADER_SIZE))) {
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
    uint16_t scalars = version >= 4u ? get16(data + 76) : 0u;
    uint64_t scalar_bool_mask = version >= 5u ? get64(data + 80) : 0u;
    uint16_t function_count =
        (version == 8u || version == 9u) ? get16(data + 88) : 0u;
    uint16_t function_record_size =
        (version == 8u || version == 9u) ? get16(data + 90) : 0u;
    uint32_t main_entry_pc =
        (version == 8u || version == 9u) ? get32(data + 92) : 0u;
    uint16_t input_count = version == 9u ? get16(data + 96) : 0u;
    uint16_t input_record_size = version == 9u ? get16(data + 98) : 0u;
    uint16_t input_abi_version = version == 9u ? get16(data + 100) : 0u;
    uint16_t input_reserved = version == 9u ? get16(data + 102) : 0u;

    if (qubits > QN_MAX_QUBITS ||
        registers > QN_MAX_REGISTERS ||
        scalars > QN_MAX_SCALARS ||
        function_count > QN_MAX_FUNCTIONS ||
        (version == 8u && (function_count == 0u ||
                           function_record_size != QBC_FUNC_SIZE)) ||
        (version == 9u &&
         (function_record_size != QBC_FUNC_SIZE ||
          input_count == 0u ||
          input_count > QN_MAX_RUNTIME_INPUTS ||
          input_record_size != QBC_INPUT_SIZE ||
          input_abi_version != QN_RUNTIME_INPUT_ABI_V1 ||
          input_reserved != 0u)) ||
        (scalars < 64u && (scalar_bool_mask >> scalars) != 0u) ||
        instruction_count > QN_MAX_INSTRUCTIONS) {
        qn_diag_set(diag, 0, 0, "QBC limits invalid");
        return QN_ERR_QBC;
    }

    size_t needed =
        header_size +
        (size_t)registers * QBC_REG_SIZE +
        (size_t)function_count * QBC_FUNC_SIZE +
        (size_t)input_count * QBC_INPUT_SIZE +
        (size_t)instruction_count * QBC_INSN_SIZE;

    if (needed != size) {
        qn_diag_set(diag, 0, 0, "QBC size mismatch");
        return QN_ERR_QBC;
    }

    out->total_qubits = qubits;
    out->register_count = registers;
    out->scalar_count = scalars;
    out->scalar_bool_mask = scalar_bool_mask;
    out->function_count = function_count;
    out->main_entry_pc = main_entry_pc;
    out->input_count = input_count;
    out->input_abi_version = input_abi_version;
    out->initial_basis = get64(data + 16);
    out->default_shots = get32(data + 24);
    out->default_seed = get64(data + 28);

    if (version == 1u) {
        memcpy(out->source_digest, data + 36, 28);
    } else {
        memcpy(out->source_digest, data + 36, 32);
    }

    if (version >= 3u) {
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

    for (uint16_t i = 0u; i < function_count; ++i) {
        QNFunctionRecord *fn = &out->functions[i];
        fn->entry_pc = get32(data + at);
        fn->end_pc = get32(data + at + 4);
        fn->scalar_count = get16(data + at + 8);
        fn->param_count = data[at + 10];
        fn->flags = data[at + 11];
        at += QBC_FUNC_SIZE;
    }

    for (uint16_t i = 0u; i < input_count; ++i) {
        QNInputRecord *input = &out->inputs[i];
        memcpy(input->input_name_sha256, data + at, 32);
        input->main_scalar_slot = get16(data + at + 32);
        input->type = data[at + 34];
        input->flags = data[at + 35];
        if (input->main_scalar_slot != i ||
            input->main_scalar_slot >= scalars ||
            input->type != QN_RUNTIME_INPUT_TYPE_U32 ||
            input->flags != 0u) {
            qn_diag_set_code(diag, "QN-E7612", 0, 0,
                             "invalid QBC runtime input metadata");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        for (uint16_t j = 0u; j < i; ++j) {
            if (memcmp(input->input_name_sha256,
                       out->inputs[j].input_name_sha256, 32) == 0) {
                qn_diag_set_code(diag, "QN-E7612", 0, 0,
                                 "duplicate QBC runtime input digest");
                qn_bytecode_free(out);
                return QN_ERR_QBC;
            }
        }
        at += QBC_INPUT_SIZE;
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

        switch (ins->opcode) {
            case OP_H:
            case OP_X:
            case OP_Z:
            case OP_CX:
            case OP_MEASURE_ALL:
            case OP_EMIT:
            case OP_U32_VECTOR_ADD:
            case OP_U32_CONST:
            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
            case OP_U32_EQ:
            case OP_U32_NE:
            case OP_U32_LT:
            case OP_U32_LE:
            case OP_U32_GT:
            case OP_U32_GE:
            case OP_U32_EMIT:
            case OP_BOOL_EMIT:
            case OP_JUMP_IF_FALSE:
            case OP_JUMP:
            case OP_U32_SET_ADD:
            case OP_U32_SET_SUB:
            case OP_U32_SET_MUL:
            case OP_U32_SET_DIV:
            case OP_REPEAT_ENTER:
            case OP_REPEAT_NEXT:
            case OP_CALL:
            case OP_RETURN:
            case OP_END:
                break;
            default:
                qn_diag_set(diag, 0, 0,
                            "unknown QBC opcode 0x%02x", ins->opcode);
                qn_bytecode_free(out);
                return QN_ERR_QBC;
        }

        if (ins->opcode == OP_U32_VECTOR_ADD &&
            (ins->a != 0u ||
             ins->b != 0u ||
             ins->flags != 0u ||
             ins->imm != QN_U32_VECTOR_ADD_COUNT)) {
            qn_diag_set_code(
                diag,
                "QN-E7406",
                0,
                0,
                "invalid bounded uint32 vector-add bytecode"
            );
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if ((ins->opcode == OP_U32_CONST ||
             ins->opcode == OP_U32_ADD ||
             ins->opcode == OP_U32_SUB ||
             ins->opcode == OP_U32_MUL ||
             ins->opcode == OP_U32_DIV ||
             ins->opcode == OP_U32_EMIT) &&
            version != 4u && version != 5u && version != 6u &&
            version != 7u && version != 8u && version != 9u) {
            qn_diag_set_code(diag, "QN-E7509", 0, 0,
                             "u32 scalar opcode requires QBC version 4 through 9");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if ((ins->opcode == OP_U32_EQ ||
             ins->opcode == OP_U32_NE ||
             ins->opcode == OP_U32_LT ||
             ins->opcode == OP_U32_LE ||
             ins->opcode == OP_U32_GT ||
             ins->opcode == OP_U32_GE ||
             ins->opcode == OP_BOOL_EMIT) &&
            version != 5u && version != 6u && version != 7u) {
            qn_diag_set_code(diag, "QN-E7523", 0, 0,
                             "comparison/bool opcode requires QBC version 5, 6 or 7");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if ((ins->opcode == OP_CALL || ins->opcode == OP_RETURN) &&
            version != 8u && version != 9u) {
            qn_diag_set_code(diag, "QN-E7595", 0, 0,
                             "function opcode requires QBC version 8 or 9");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if ((repeat_set_opcode(ins->opcode) ||
             ins->opcode == OP_REPEAT_ENTER ||
             ins->opcode == OP_REPEAT_NEXT) && version != 7u) {
            qn_diag_set_code(diag, "QN-E7567", 0, 0,
                             "bounded repeat opcode requires QBC version 7");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if (repeat_set_opcode(ins->opcode) &&
            (ins->a >= scalars || ins->b >= scalars ||
             ins->flags >= scalars || ins->imm != 0u ||
             (scalar_bool_mask & (UINT64_C(1) << ins->a)) != 0u ||
             (scalar_bool_mask & (UINT64_C(1) << ins->b)) != 0u ||
             (scalar_bool_mask & (UINT64_C(1) << ins->flags)) != 0u)) {
            qn_diag_set_code(diag, "QN-E7568", 0, 0,
                             "invalid bounded repeat set bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if (ins->opcode == OP_REPEAT_ENTER) {
            uint32_t repeat_count = (uint32_t)ins->a |
                                    ((uint32_t)ins->b << 8);
            if (repeat_count == 0u ||
                repeat_count > QN_MAX_REPEAT_ITERATIONS ||
                ins->flags != 0u || ins->imm <= i + 1u ||
                ins->imm >= instruction_count) {
                qn_diag_set_code(diag, "QN-E7569", 0, 0,
                                 "invalid bounded repeat enter bytecode");
                qn_bytecode_free(out);
                return QN_ERR_QBC;
            }
        }
        if (ins->opcode == OP_REPEAT_NEXT &&
            (ins->a != 0u || ins->b != 0u || ins->flags != 0u ||
             ins->imm >= i)) {
            qn_diag_set_code(diag, "QN-E7569", 0, 0,
                             "invalid bounded repeat next bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if ((ins->opcode == OP_JUMP_IF_FALSE ||
             ins->opcode == OP_JUMP) && version != 6u) {
            qn_diag_set_code(diag, "QN-E7540", 0, 0,
                             "control-flow opcode requires QBC version 6");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if (ins->opcode == OP_JUMP_IF_FALSE &&
            (ins->a >= scalars ||
             (scalar_bool_mask & (UINT64_C(1) << ins->a)) == 0u ||
             ins->b != 0u || ins->flags != 0u ||
             ins->imm <= i || ins->imm >= instruction_count)) {
            qn_diag_set_code(diag, "QN-E7541", 0, 0,
                             "invalid or non-forward conditional jump bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if (ins->opcode == OP_JUMP &&
            (ins->a != 0u || ins->b != 0u || ins->flags != 0u ||
             ins->imm <= i || ins->imm >= instruction_count)) {
            qn_diag_set_code(diag, "QN-E7541", 0, 0,
                             "invalid or non-forward jump bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if (version != 8u && ins->opcode == OP_U32_CONST &&
            (ins->a >= scalars || ins->b != 0u ||
             ins->flags != 0u)) {
            qn_diag_set_code(diag, "QN-E7509", 0, 0,
                             "invalid u32 constant bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if (version != 8u && (ins->opcode == OP_U32_ADD ||
             ins->opcode == OP_U32_SUB ||
             ins->opcode == OP_U32_MUL ||
             ins->opcode == OP_U32_DIV) &&
            (ins->a >= scalars || ins->b >= scalars ||
             ins->flags >= scalars || ins->imm != 0u)) {
            qn_diag_set_code(diag, "QN-E7509", 0, 0,
                             "invalid u32 arithmetic bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if ((ins->opcode == OP_U32_EQ ||
             ins->opcode == OP_U32_NE ||
             ins->opcode == OP_U32_LT ||
             ins->opcode == OP_U32_LE ||
             ins->opcode == OP_U32_GT ||
             ins->opcode == OP_U32_GE) &&
            (ins->a >= scalars || ins->b >= scalars ||
             ins->flags >= scalars || ins->imm != 0u)) {
            qn_diag_set_code(diag, "QN-E7524", 0, 0,
                             "invalid u32 comparison bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
        if (ins->opcode == OP_BOOL_EMIT &&
            (ins->a >= scalars || ins->b != 0u ||
             ins->flags != 0u || ins->imm != 0u)) {
            qn_diag_set_code(diag, "QN-E7525", 0, 0,
                             "invalid bool emit bytecode");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }

        if (version != 8u && version != 9u && ins->opcode == OP_U32_EMIT &&
            (ins->a >= scalars || ins->b != 0u ||
             ins->flags != 0u || ins->imm != 0u)) {
            qn_diag_set_code(diag, "QN-E7509", 0, 0,
                             "invalid u32 emit bytecode");
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

    bool contains_vector_opcode = false;
    bool contains_scalar_opcode = false;
    bool contains_control_flow = false;
    bool contains_repeat = false;
    bool contains_functions = false;
    for (size_t i = 0; i < out->instruction_count; ++i) {
        uint8_t opcode = out->instructions[i].opcode;
        if (opcode == OP_U32_VECTOR_ADD) contains_vector_opcode = true;
        if (opcode == OP_U32_CONST || opcode == OP_U32_ADD ||
            opcode == OP_U32_SUB || opcode == OP_U32_MUL ||
            opcode == OP_U32_DIV || opcode == OP_U32_EQ ||
            opcode == OP_U32_NE || opcode == OP_U32_LT ||
            opcode == OP_U32_LE || opcode == OP_U32_GT ||
            opcode == OP_U32_GE || opcode == OP_U32_EMIT ||
            opcode == OP_BOOL_EMIT ||
            opcode == OP_JUMP_IF_FALSE || opcode == OP_JUMP ||
            repeat_set_opcode(opcode) || opcode == OP_REPEAT_ENTER ||
            opcode == OP_REPEAT_NEXT || opcode == OP_CALL ||
            opcode == OP_RETURN)
            contains_scalar_opcode = true;
        if (opcode == OP_JUMP_IF_FALSE || opcode == OP_JUMP)
            contains_control_flow = true;
        if (repeat_set_opcode(opcode) || opcode == OP_REPEAT_ENTER ||
            opcode == OP_REPEAT_NEXT)
            contains_repeat = true;
        if (opcode == OP_CALL || opcode == OP_RETURN)
            contains_functions = true;
    }

    if (contains_vector_opcode) {
        if (version < 3u || contains_scalar_opcode ||
            !qn_qbc_is_bounded_u32_vector_add(out)) {
            qn_diag_set_code(diag, "QN-E7406", 0, 0,
                             "QBC bounded vector-add contract invalid");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
    } else if (contains_scalar_opcode) {
        bool valid = false;
        if (version == 4u) {
            valid = out->scalar_bool_mask == 0u &&
                    !contains_control_flow &&
                    qn_qbc_is_u32_scalar_program(out);
        } else if (version == 5u) {
            valid = out->scalar_bool_mask != 0u &&
                    !contains_control_flow &&
                    qn_qbc_is_typed_scalar_program(out);
        } else if (version == 6u) {
            valid = out->scalar_bool_mask != 0u &&
                    contains_control_flow && !contains_repeat &&
                    qn_qbc_is_typed_scalar_program(out);
        } else if (version == 7u) {
            valid = contains_repeat && !contains_control_flow &&
                    qn_qbc_is_typed_scalar_program(out);
        }
        else if (version == 8u) {
            valid = contains_functions && !contains_repeat &&
                    !contains_control_flow && out->scalar_bool_mask == 0u &&
                    out->input_count == 0u &&
                    qn_qbc_is_typed_scalar_program(out);
        } else if (version == 9u) {
            valid = out->input_count > 0u &&
                    !contains_repeat && !contains_control_flow &&
                    out->scalar_bool_mask == 0u &&
                    qn_qbc_is_typed_scalar_program(out);
        }
        if (!valid) {
            qn_diag_set_code(diag, "QN-E7509", 0, 0,
                             "QBC typed scalar program contract invalid");
            qn_bytecode_free(out);
            return QN_ERR_QBC;
        }
    } else if (out->total_qubits == 0u) {
        qn_diag_set(diag, 0, 0,
                    "QBC declares zero qubits without native compute operation");
        qn_bytecode_free(out);
        return QN_ERR_QBC;
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
