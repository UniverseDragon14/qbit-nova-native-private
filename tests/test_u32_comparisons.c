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
    bc.scalar_count = 4u;
    bc.scalar_bool_mask = UINT64_C(0x0c);
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    bc.instruction_count = 6u;
    bc.instructions = calloc(
        bc.instruction_count,
        sizeof(*bc.instructions)
    );
    if (!bc.instructions) return fail("allocation");

    bc.instructions[0] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=0u, .imm=10u
    };
    bc.instructions[1] = (QNInstruction){
        .opcode=OP_U32_CONST, .a=1u, .imm=20u
    };
    bc.instructions[2] = (QNInstruction){
        .opcode=OP_U32_LT, .a=2u, .b=0u, .flags=1u
    };
    bc.instructions[3] = (QNInstruction){
        .opcode=OP_U32_GE, .a=3u, .b=1u, .flags=0u
    };
    bc.instructions[4] = (QNInstruction){
        .opcode=OP_BOOL_EMIT, .a=2u
    };
    bc.instructions[5] = (QNInstruction){.opcode=OP_END};

    if (!qn_qbc_is_typed_scalar_program(&bc))
        return fail("valid typed scalar comparison contract rejected");
    if (qn_qbc_is_u32_scalar_program(&bc))
        return fail("bool program misclassified as pure u32 scalar");

    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK)
        return fail("encode failed");
    if (encoded_size != 136u)
        return fail("unexpected comparison QBC size");
    if (encoded[4] != 5u || encoded[5] != 0u)
        return fail("comparison QBC is not version 5");
    if (encoded[76] != 4u || encoded[77] != 0u)
        return fail("scalar count not encoded");
    if (encoded[80] != 0x0cu)
        return fail("bool mask not encoded");
    if (encoded[104] != OP_U32_LT ||
        encoded[112] != OP_U32_GE ||
        encoded[120] != OP_BOOL_EMIT) {
        return fail("comparison opcode layout mismatch");
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
    if (!qn_qbc_is_typed_scalar_program(&decoded))
        return fail("decoded comparison contract rejected");
    if (decoded.scalar_bool_mask != UINT64_C(0x0c))
        return fail("decoded bool mask mismatch");

    decoded.scalar_bool_mask |= UINT64_C(1) << 0;
    if (qn_qbc_is_typed_scalar_program(&decoded))
        return fail("bool input accepted by u32 comparison");
    decoded.scalar_bool_mask = UINT64_C(0x0c);

    decoded.instructions[4].opcode = OP_U32_EMIT;
    if (qn_qbc_is_typed_scalar_program(&decoded))
        return fail("bool slot accepted by u32 emit");
    decoded.instructions[4].opcode = OP_BOOL_EMIT;

    QNInstruction legacy_instructions[5] = {
        {.opcode=OP_U32_CONST, .a=0u, .imm=10u},
        {.opcode=OP_U32_CONST, .a=1u, .imm=20u},
        {.opcode=OP_U32_LT, .a=2u, .b=0u, .flags=1u},
        {.opcode=OP_U32_EMIT, .a=2u},
        {.opcode=OP_END}
    };
    QNBytecode legacy;
    memset(&legacy, 0, sizeof(legacy));
    legacy.scalar_count = 3u;
    legacy.default_shots = 1u;
    legacy.default_seed = 1u;
    legacy.capability_mask =
        QN_CAP_COMPUTE_U32_SCALAR | QN_CAP_EVIDENCE_EMIT;
    legacy.instructions = legacy_instructions;
    legacy.instruction_count = 5u;

    uint8_t *legacy_encoded = NULL;
    size_t legacy_size = 0u;
    if (qn_qbc_encode(
            &legacy, &legacy_encoded, &legacy_size, &diag
        ) != QN_OK) {
        return fail("legacy-version encode failed");
    }
    if (legacy_encoded[4] != 4u)
        return fail("legacy comparison fixture is not QBC v4");

    QNBytecode legacy_invalid;
    memset(&legacy_invalid, 0, sizeof(legacy_invalid));
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_decode(
            legacy_encoded, legacy_size, &legacy_invalid, &diag
        ) == QN_OK) {
        qn_bytecode_free(&legacy_invalid);
        free(legacy_encoded);
        return fail("QBC v4 accepted comparison opcode");
    }
    if (strcmp(diag.code, "QN-E7523") != 0) {
        free(legacy_encoded);
        return fail("QBC v4 comparison did not fail with QN-E7523");
    }
    free(legacy_encoded);

    uint8_t saved_version = encoded[4];
    encoded[4] = 4u;
    QNBytecode invalid;
    memset(&invalid, 0, sizeof(invalid));
    if (qn_qbc_decode(
            encoded,
            encoded_size,
            &invalid,
            &diag
        ) == QN_OK) {
        qn_bytecode_free(&invalid);
        return fail("QBC v4 accepted comparison/bool format");
    }
    encoded[4] = saved_version;

    qn_bytecode_free(&decoded);
    free(encoded);
    qn_bytecode_free(&bc);

    puts("PASS: QBIT_NOVA_U32_COMPARISONS_BOOL_UNIT_V07_STEP3");
    return 0;
}
