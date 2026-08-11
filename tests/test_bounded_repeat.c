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
        .opcode=OP_U32_CONST, .a=0u, .imm=0u
    };
    instructions[1] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=1u, .imm=1u
    };
    instructions[2] = (QNInstruction){
        .opcode=OP_REPEAT_ENTER, .a=4u, .b=0u, .imm=5u
    };
    instructions[3] = (QNInstruction){
        .opcode=OP_U32_SET_ADD, .a=0u, .b=0u, .flags=1u
    };
    instructions[4] = (QNInstruction){
        .opcode=OP_REPEAT_NEXT, .imm=2u
    };
    instructions[5] = (QNInstruction){
        .opcode=OP_U32_EMIT, .a=0u
    };
    instructions[6] = (QNInstruction){.opcode=OP_END};

    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.scalar_count = 2u;
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

    if (!qn_qbc_has_bounded_repeat(&bc))
        return fail("bounded repeat not detected");
    if (!qn_qbc_has_control_flow(&bc))
        return fail("bounded repeat not classified as control flow");
    if (!qn_qbc_is_typed_scalar_program(&bc))
        return fail("valid bounded repeat contract rejected");

    uint64_t steps = 0u;
    if (!qn_qbc_execution_step_bound(&bc, &steps) || steps != 16u)
        return fail("bounded repeat execution step bound mismatch");

    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK)
        return fail("QBC v7 encode failed");
    if (encoded_size != 144u)
        return fail("unexpected QBC v7 size");
    if (encoded[4] != 7u || encoded[5] != 0u)
        return fail("bounded repeat QBC is not version 7");
    if (encoded[6] != 88u || encoded[7] != 0u)
        return fail("QBC v7 header size changed");
    if (encoded[104] != OP_REPEAT_ENTER ||
        encoded[112] != OP_U32_SET_ADD ||
        encoded[120] != OP_REPEAT_NEXT)
        return fail("QBC v7 repeat opcode layout mismatch");

    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_OK)
        return fail("QBC v7 decode failed");
    if (!qn_qbc_is_typed_scalar_program(&decoded))
        return fail("decoded QBC v7 contract rejected");
    qn_bytecode_free(&decoded);

    instructions[2].a = 0u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("repeat count zero accepted");
    bc = make_valid(instructions);

    instructions[4].imm = 1u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("repeat target mismatch accepted");
    bc = make_valid(instructions);

    instructions[3].opcode = OP_U32_EMIT;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("emit inside repeat accepted");
    bc = make_valid(instructions);

    instructions[3].opcode = OP_JUMP;
    instructions[3].imm = 2u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("general backward jump accepted inside repeat");
    bc = make_valid(instructions);

    instructions[3].a = 2u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("out-of-range mutation target accepted");
    bc = make_valid(instructions);

    size_t body_count = 1000u;
    size_t count = body_count + 6u;
    QNInstruction *large = calloc(count, sizeof(*large));
    if (!large) return fail("allocation failed");
    large[0] = (QNInstruction){.opcode=OP_U32_CONST,.a=0u,.imm=0u};
    large[1] = (QNInstruction){.opcode=OP_U32_CONST,.a=1u,.imm=1u};
    large[2] = (QNInstruction){
        .opcode=OP_REPEAT_ENTER,.a=0u,.b=4u,
        .imm=(uint32_t)(3u + body_count)
    };
    for (size_t i = 0; i < body_count; ++i) {
        large[3u + i] = (QNInstruction){
            .opcode=OP_U32_SET_ADD,.a=0u,.b=0u,.flags=1u
        };
    }
    size_t next = 3u + body_count;
    large[next] = (QNInstruction){.opcode=OP_REPEAT_NEXT,.imm=2u};
    large[next + 1u] = (QNInstruction){.opcode=OP_U32_EMIT,.a=0u};
    large[next + 2u] = (QNInstruction){.opcode=OP_END};

    QNBytecode huge = {0};
    huge.scalar_count = 2u;
    huge.default_shots = 1u;
    huge.default_seed = 1u;
    huge.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    huge.instructions = large;
    huge.instruction_count = count;
    steps = 0u;
    if (qn_qbc_execution_step_bound(&huge, &steps)) {
        free(large);
        free(encoded);
        return fail("execution budget overflow accepted");
    }
    if (qn_qbc_is_typed_scalar_program(&huge)) {
        free(large);
        free(encoded);
        return fail("over-budget repeat contract accepted");
    }
    free(large);

    uint8_t saved_version = encoded[4];
    encoded[4] = 6u;
    memset(&decoded, 0, sizeof(decoded));
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) == QN_OK) {
        qn_bytecode_free(&decoded);
        free(encoded);
        return fail("QBC v6 accepted bounded repeat opcode");
    }
    if (strcmp(diag.code, "QN-E7567") != 0) {
        free(encoded);
        return fail("wrong version did not fail with QN-E7567");
    }
    encoded[4] = saved_version;

    free(encoded);
    puts("PASS: QBIT_NOVA_BOUNDED_REPEAT_UNIT_V07_STEP5");
    return 0;
}
