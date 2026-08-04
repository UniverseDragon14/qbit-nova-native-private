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
    bc.scalar_count = 3u;
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    bc.instruction_count = 5u;
    bc.instructions = calloc(bc.instruction_count,
                             sizeof(*bc.instructions));
    if (!bc.instructions) return fail("allocation");

    bc.instructions[0] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=0u, .imm=10u
    };
    bc.instructions[1] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=1u, .imm=20u
    };
    bc.instructions[2] = (QNInstruction){
        .opcode=OP_U32_ADD, .a=2u, .b=0u, .flags=1u
    };
    bc.instructions[3] = (QNInstruction){
        .opcode=OP_U32_EMIT, .a=2u
    };
    bc.instructions[4] = (QNInstruction){.opcode=OP_END};

    if (!qn_qbc_is_u32_scalar_program(&bc))
        return fail("valid scalar contract rejected");

    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK)
        return fail("encode failed");
    if (encoded_size != 120u)
        return fail("unexpected QBC size");
    if (encoded[4] != 4u || encoded[5] != 0u)
        return fail("scalar QBC is not version 4");
    if (encoded[76] != 3u || encoded[77] != 0u)
        return fail("scalar count not encoded");

    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_OK)
        return fail("decode failed");
    if (!qn_qbc_is_u32_scalar_program(&decoded))
        return fail("decoded scalar contract rejected");

    decoded.instructions[2].b = 63u;
    if (qn_qbc_is_u32_scalar_program(&decoded))
        return fail("out-of-range operand accepted");

    qn_bytecode_free(&decoded);
    free(encoded);
    qn_bytecode_free(&bc);

    puts("PASS: QBIT_NOVA_TYPED_U32_SCALAR_UNIT_V07_STEP1");
    return 0;
}
