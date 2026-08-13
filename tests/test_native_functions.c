#include "qn_qbc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static QNBytecode make_valid(QNInstruction instructions[7]) {
    instructions[0] = (QNInstruction){
        .opcode=OP_U32_ADD, .a=2u, .b=0u, .flags=1u
    };
    instructions[1] = (QNInstruction){
        .opcode=OP_RETURN, .a=2u
    };
    instructions[2] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=0u, .imm=10u
    };
    instructions[3] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=1u, .imm=20u
    };
    instructions[4] = (QNInstruction){
        .opcode=OP_CALL, .a=2u, .b=0u, .flags=1u, .imm=0u
    };
    instructions[5] = (QNInstruction){
        .opcode=OP_U32_EMIT, .a=2u
    };
    instructions[6] = (QNInstruction){.opcode=OP_END};

    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.scalar_count = 3u;
    bc.function_count = 1u;
    bc.main_entry_pc = 2u;
    bc.functions[0] = (QNFunctionRecord){
        .entry_pc=0u, .end_pc=2u, .scalar_count=3u,
        .param_count=2u, .flags=0u
    };
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    bc.instructions = instructions;
    bc.instruction_count = 7u;
    return bc;
}

static QNBytecode make_cycle(QNInstruction instructions[7]) {
    instructions[0] = (QNInstruction){
        .opcode=OP_CALL, .a=0u, .imm=1u
    };
    instructions[1] = (QNInstruction){.opcode=OP_RETURN, .a=0u};
    instructions[2] = (QNInstruction){
        .opcode=OP_CALL, .a=0u, .imm=0u
    };
    instructions[3] = (QNInstruction){.opcode=OP_RETURN, .a=0u};
    instructions[4] = (QNInstruction){
        .opcode=OP_CALL, .a=0u, .imm=0u
    };
    instructions[5] = (QNInstruction){.opcode=OP_U32_EMIT, .a=0u};
    instructions[6] = (QNInstruction){.opcode=OP_END};

    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.scalar_count = 1u;
    bc.function_count = 2u;
    bc.main_entry_pc = 4u;
    bc.functions[0] = (QNFunctionRecord){
        .entry_pc=0u, .end_pc=2u, .scalar_count=1u
    };
    bc.functions[1] = (QNFunctionRecord){
        .entry_pc=2u, .end_pc=4u, .scalar_count=1u
    };
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    bc.instructions = instructions;
    bc.instruction_count = 7u;
    return bc;
}

int main(void) {
    QNInstruction instructions[7];
    QNBytecode bc = make_valid(instructions);

    if (!qn_qbc_has_functions(&bc))
        return fail("function program not detected");
    if (qn_qbc_has_bounded_repeat(&bc))
        return fail("function program misclassified as repeat");
    if (qn_qbc_has_control_flow(&bc))
        return fail("function program misclassified as jump control flow");
    if (!qn_qbc_is_typed_scalar_program(&bc))
        return fail("valid function contract rejected");
    if (qn_qbc_is_u32_scalar_program(&bc))
        return fail("function program leaked into legacy u32 scalar contract");

    uint64_t steps = 0u;
    if (!qn_qbc_execution_step_bound(&bc, &steps) || steps != 7u)
        return fail("expanded function execution bound mismatch");

    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK)
        return fail("QBC v8 encode failed");
    if (encoded_size != 164u)
        return fail("unexpected QBC v8 size");
    if (encoded[4] != 8u || encoded[5] != 0u ||
        encoded[6] != 96u || encoded[7] != 0u)
        return fail("QBC v8 version/header mismatch");
    if (encoded[88] != 1u || encoded[89] != 0u ||
        encoded[90] != 12u || encoded[91] != 0u)
        return fail("QBC v8 function header fields mismatch");
    if (encoded[92] != 2u || encoded[93] != 0u ||
        encoded[94] != 0u || encoded[95] != 0u)
        return fail("QBC v8 main entry mismatch");
    if (encoded[96] != 0u || encoded[100] != 2u ||
        encoded[104] != 3u || encoded[106] != 2u || encoded[107] != 0u)
        return fail("QBC v8 function record layout mismatch");
    if (encoded[108] != OP_U32_ADD || encoded[116] != OP_RETURN ||
        encoded[140] != OP_CALL || encoded[156] != OP_END)
        return fail("QBC v8 instruction layout mismatch");

    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_OK)
        return fail("QBC v8 decode failed");
    if (!qn_qbc_is_typed_scalar_program(&decoded) ||
        decoded.function_count != 1u || decoded.main_entry_pc != 2u ||
        decoded.functions[0].entry_pc != 0u ||
        decoded.functions[0].end_pc != 2u ||
        decoded.functions[0].scalar_count != 3u ||
        decoded.functions[0].param_count != 2u)
        return fail("decoded QBC v8 metadata mismatch");
    qn_bytecode_free(&decoded);

    bc.functions[0].flags = 1u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("nonzero function flags accepted");
    bc = make_valid(instructions);

    bc.scalar_bool_mask = 1u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("bool scalar mask accepted by u32 function contract");
    bc = make_valid(instructions);

    bc.functions[0].end_pc = 3u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("function range crossing main entry accepted");
    bc = make_valid(instructions);

    instructions[4].imm = 1u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("invalid CALL function index accepted");
    bc = make_valid(instructions);

    instructions[1].opcode = OP_END;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("END inside function accepted");
    bc = make_valid(instructions);

    instructions[1] = (QNInstruction){.opcode=OP_RETURN,.a=3u};
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("out-of-range RETURN accepted");
    bc = make_valid(instructions);

    instructions[4] = (QNInstruction){.opcode=OP_RETURN,.a=0u};
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("RETURN outside function range accepted");
    bc = make_valid(instructions);

    QNInstruction cycle_ins[7];
    QNBytecode cycle = make_cycle(cycle_ins);
    if (qn_qbc_is_typed_scalar_program(&cycle))
        return fail("indirect recursive QBC call graph accepted");
    steps = 0u;
    if (qn_qbc_execution_step_bound(&cycle, &steps))
        return fail("recursive QBC received execution bound");

    uint8_t saved_record_size = encoded[90];
    encoded[90] = 11u;
    memset(&decoded, 0, sizeof(decoded));
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) == QN_OK) {
        qn_bytecode_free(&decoded);
        free(encoded);
        return fail("invalid QBC v8 function record size accepted");
    }
    encoded[90] = saved_record_size;

    uint8_t saved_version = encoded[4];
    encoded[4] = 7u;
    memset(&decoded, 0, sizeof(decoded));
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) == QN_OK) {
        qn_bytecode_free(&decoded);
        free(encoded);
        return fail("QBC v7 accepted QBC v8 layout");
    }
    encoded[4] = saved_version;

    free(encoded);
    puts("PASS: QBIT_NOVA_NATIVE_FUNCTIONS_UNIT_V07_STEP6");
    return 0;
}
