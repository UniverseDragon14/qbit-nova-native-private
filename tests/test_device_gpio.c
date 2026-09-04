#include "qn_device.h"
#include "qn_guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static QNBytecode valid_gpio_program(void) {
    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask = QN_CAP_DEVICE_CONTROL | QN_CAP_EVIDENCE_EMIT;
    bc.instruction_count = 4u;
    bc.instructions = calloc(bc.instruction_count, sizeof(*bc.instructions));
    if (!bc.instructions) fail("allocation");
    bc.instructions[0] = (QNInstruction){
        .opcode = OP_GPIO_CONFIG, .a = 0u, .imm = 21u
    };
    bc.instructions[1] = (QNInstruction){
        .opcode = OP_GPIO_WRITE, .a = 0u, .b = 1u
    };
    bc.instructions[2] = (QNInstruction){
        .opcode = OP_DEVICE_EMIT, .a = 0u
    };
    bc.instructions[3] = (QNInstruction){.opcode = OP_END};
    return bc;
}

int main(void) {
    QNBytecode bc = valid_gpio_program();
    if (!qn_qbc_is_gpio_output_program(&bc)) fail("valid contract rejected");

    QNGuardPolicy policy;
    QNDiagnostic diag = {0};
    qn_guard_policy_safe(&policy);
    if (qn_guard_enforce(bc.capability_mask, &policy, &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E-APPROVAL-001") != 0) {
        fail("device control did not require approval");
    }
    policy.approved |= QN_CAP_DEVICE_CONTROL;
    memset(&diag, 0, sizeof(diag));
    if (qn_guard_enforce(bc.capability_mask, &policy, &diag) != QN_OK)
        fail("approved device control rejected");

    QNDeviceOptions options;
    qn_device_options_safe(&options);
    options.backend = QN_DEVICE_BACKEND_MOCK;
    options.hold_ms = 0u;
    QNDeviceResult result;
    if (qn_device_execute_gpio(&bc, &options, &result, &diag) != QN_OK)
        fail("mock execution failed");
    if (!result.mock || !result.write_executed || !result.reset_low ||
        result.line_offset != 21u || !result.value_high)
        fail("mock evidence invalid");

    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    memset(&diag, 0, sizeof(diag));
    if (qn_qbc_encode(&bc, &encoded, &encoded_size, &diag) != QN_OK ||
        !encoded || encoded_size != 136u || encoded[4] != 10u ||
        encoded[5] != 0u) fail("QBC v10 encode failed");
    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_OK ||
        !qn_qbc_is_gpio_output_program(&decoded))
        fail("QBC v10 roundtrip failed");
    qn_bytecode_free(&decoded);

    encoded[103] = 1u;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_ERR_QBC)
        fail("nonzero QBC v10 reserved byte accepted");
    encoded[103] = 0u;
    encoded[114] = 2u;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_ERR_QBC)
        fail("tampered GPIO write accepted");

    encoded[12] = 1u;
    memset(encoded + 68, 0, 8u);
    encoded[68] = (uint8_t)(QN_CAP_QUANTUM_SIMULATE | QN_CAP_EVIDENCE_EMIT);
    memset(encoded + 104, 0, 24u);
    encoded[104] = OP_H;
    encoded[112] = OP_MEASURE_ALL;
    encoded[120] = OP_EMIT;
    memset(&decoded, 0, sizeof(decoded));
    if (qn_qbc_decode(encoded, encoded_size, &decoded, &diag) != QN_ERR_QBC)
        fail("non-device QBC v10 accepted");
    free(encoded);

    options.backend = QN_DEVICE_BACKEND_DENY;
    memset(&diag, 0, sizeof(diag));
    if (qn_device_execute_gpio(&bc, &options, &result, &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E7811") != 0)
        fail("deny backend did not fail closed");

    bc.instructions[0].imm = QN_MAX_GPIO_LINE_OFFSET + 1u;
    if (qn_qbc_is_gpio_output_program(&bc)) fail("oversized line accepted");
    bc.instructions[0].imm = 21u;
    bc.instructions[1].b = 2u;
    if (qn_qbc_is_gpio_output_program(&bc)) fail("non-boolean write accepted");

    qn_bytecode_free(&bc);
    puts("DEVICE_GPIO_CONTRACT_TESTS=PASS");
    return 0;
}
