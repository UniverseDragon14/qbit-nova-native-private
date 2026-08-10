#include "qn_qbc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.scalar_count = 6u;
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    bc.instruction_count = 8u;
    bc.instructions = calloc(
        bc.instruction_count,
        sizeof(*bc.instructions)
    );
    if (!bc.instructions) return fail("allocation");

    bc.instructions[0] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=0u, .imm=100u
    };
    bc.instructions[1] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=1u, .imm=4u
    };
    bc.instructions[2] = (QNInstruction){
        .opcode=OP_U32_SUB, .a=2u, .b=0u, .flags=1u
    };
    bc.instructions[3] = (QNInstruction){
        .opcode=OP_U32_MUL, .a=3u, .b=1u, .flags=1u
    };
    bc.instructions[4] = (QNInstruction){
        .opcode=OP_U32_DIV, .a=4u, .b=0u, .flags=1u
    };
    bc.instructions[5] = (QNInstruction){
        .opcode=OP_U32_ADD, .a=5u, .b=2u, .flags=3u
    };
    bc.instructions[6] = (QNInstruction){
        .opcode=OP_U32_EMIT, .a=5u
    };
    bc.instructions[7] = (QNInstruction){.opcode=OP_END};

    if (!qn_qbc_is_u32_scalar_program(&bc))
        return fail("valid arithmetic contract rejected");

    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK)
        return fail("encode failed");
    if (encoded_size != 144u)
        return fail("unexpected arithmetic QBC size");
    if (encoded[4] != 4u || encoded[5] != 0u)
        return fail("arithmetic QBC is not version 4");
    if (encoded[76] != 6u || encoded[77] != 0u)
        return fail("scalar count not encoded");
    if (encoded[96] != OP_U32_SUB ||
        encoded[104] != OP_U32_MUL ||
        encoded[112] != OP_U32_DIV) {
        return fail("arithmetic opcode layout mismatch");
    }

    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(
            encoded,
            encoded_size,
            &decoded,
            &diag
        ) != QN_OK) {
        return fail("decode failed");
    }
    if (!qn_qbc_is_u32_scalar_program(&decoded))
        return fail("decoded arithmetic contract rejected");

    decoded.instructions[4].flags = 63u;
    if (qn_qbc_is_u32_scalar_program(&decoded))
        return fail("out-of-range division operand accepted");
    decoded.instructions[4].flags = 1u;

    uint8_t saved = encoded[96];
    encoded[96] = 0x60u;
    QNBytecode invalid;
    memset(&invalid, 0, sizeof(invalid));
    if (qn_qbc_decode(
            encoded,
            encoded_size,
            &invalid,
            &diag
        ) == QN_OK) {
        qn_bytecode_free(&invalid);
        return fail("unknown arithmetic opcode accepted");
    }
    encoded[96] = saved;

    qn_bytecode_free(&decoded);
    free(encoded);
    qn_bytecode_free(&bc);

    puts("PASS: QBIT_NOVA_U32_ARITHMETIC_UNIT_V07_STEP2");
    return 0;
}
