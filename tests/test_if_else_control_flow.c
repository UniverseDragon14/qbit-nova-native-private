#include "qn_qbc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static QNBytecode make_valid(QNInstruction instructions[8]) {
    instructions[0] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=0u, .imm=10u
    };
    instructions[1] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=1u, .imm=20u
    };
    instructions[2] = (QNInstruction){
        .opcode=OP_U32_LT, .a=2u, .b=0u, .flags=1u
    };
    instructions[3] = (QNInstruction){
        .opcode=OP_JUMP_IF_FALSE, .a=2u, .imm=6u
    };
    instructions[4] = (QNInstruction){
        .opcode=OP_U32_EMIT, .a=0u
    };
    instructions[5] = (QNInstruction){
        .opcode=OP_JUMP, .imm=7u
    };
    instructions[6] = (QNInstruction){
        .opcode=OP_U32_EMIT, .a=1u
    };
    instructions[7] = (QNInstruction){.opcode=OP_END};

    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.scalar_count = 3u;
    bc.scalar_bool_mask = UINT64_C(0x04);
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    bc.instructions = instructions;
    bc.instruction_count = 8u;
    return bc;
}

int main(void) {
    QNInstruction instructions[8];
    QNBytecode bc = make_valid(instructions);

    if (!qn_qbc_has_control_flow(&bc))
        return fail("control-flow program not detected");
    if (!qn_qbc_is_typed_scalar_program(&bc))
        return fail("valid forward if/else contract rejected");

    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK)
        return fail("QBC v6 encode failed");
    if (encoded_size != 152u)
        return fail("unexpected QBC v6 size");
    if (encoded[4] != 6u || encoded[5] != 0u)
        return fail("if/else QBC is not version 6");
    if (encoded[6] != 88u || encoded[7] != 0u)
        return fail("QBC v6 header size changed");
    if (encoded[76] != 3u || encoded[77] != 0u)
        return fail("QBC v6 scalar count mismatch");
    if (encoded[80] != 0x04u)
        return fail("QBC v6 bool mask mismatch");
    if (encoded[112] != OP_JUMP_IF_FALSE ||
        encoded[128] != OP_JUMP)
        return fail("QBC v6 jump opcode layout mismatch");

    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_OK)
        return fail("QBC v6 decode failed");
    if (!qn_qbc_is_typed_scalar_program(&decoded))
        return fail("decoded QBC v6 contract rejected");
    qn_bytecode_free(&decoded);

    instructions[3].imm = 3u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("backward/self conditional jump accepted");
    bc = make_valid(instructions);

    instructions[3].a = 0u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("u32 condition accepted by conditional jump");
    bc = make_valid(instructions);

    instructions[5].imm = 5u;
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("backward/self unconditional jump accepted");
    bc = make_valid(instructions);

    instructions[4] = (QNInstruction){
        .opcode=OP_BOOL_EMIT, .a=2u
    };
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("branch output type mismatch accepted");
    bc = make_valid(instructions);

    instructions[6] = (QNInstruction){
        .opcode=OP_JUMP, .imm=7u
    };
    if (qn_qbc_is_typed_scalar_program(&bc))
        return fail("branch without emit accepted");
    bc = make_valid(instructions);

    uint8_t saved_version = encoded[4];
    encoded[4] = 5u;
    memset(&decoded, 0, sizeof(decoded));
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) == QN_OK) {
        qn_bytecode_free(&decoded);
        free(encoded);
        return fail("QBC v5 accepted control-flow opcode");
    }
    encoded[4] = saved_version;

    encoded[116] = 3u;
    encoded[117] = 0u;
    encoded[118] = 0u;
    encoded[119] = 0u;
    memset(&decoded, 0, sizeof(decoded));
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) == QN_OK) {
        qn_bytecode_free(&decoded);
        free(encoded);
        return fail("decoder accepted non-forward conditional jump");
    }
    if (strcmp(diag.code, "QN-E7541") != 0) {
        free(encoded);
        return fail("non-forward jump did not fail with QN-E7541");
    }

    free(encoded);
    puts("PASS: QBIT_NOVA_IF_ELSE_CONTROL_FLOW_UNIT_V07_STEP4");
    return 0;
}
