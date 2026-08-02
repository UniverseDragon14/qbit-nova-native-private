#include "qn_gpu_routing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int require_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static QNBytecode make_bounded_compute(void) {
    static QNInstruction instructions[3];
    memset(instructions, 0, sizeof(instructions));
    instructions[0].opcode = OP_U32_VECTOR_ADD;
    instructions[0].imm = QN_U32_VECTOR_ADD_COUNT;
    instructions[1].opcode = OP_EMIT;
    instructions[2].opcode = OP_END;

    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.default_shots = 1u;
    bc.default_seed = 1u;
    bc.capability_mask =
        QN_CAP_COMPUTE_U32_ADD | QN_CAP_EVIDENCE_EMIT;
    bc.instructions = instructions;
    bc.instruction_count = 3u;
    return bc;
}

int main(void) {
    QNBytecode bc = make_bounded_compute();
    QNDiagnostic diag;
    QNGpuQvmRoute route;
    QNStatus status;

    if (require_true(
            qn_qbc_is_bounded_u32_vector_add(&bc),
            "exact bounded compute bytecode must be recognized")) {
        return 1;
    }

    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    memset(&diag, 0, sizeof(diag));
    status = qn_qbc_encode(
        &bc,
        &encoded,
        &encoded_size,
        &diag
    );
    if (require_true(status == QN_OK,
                     "bounded compute QBC encode must succeed") ||
        require_true(encoded_size == 104u,
                     "bounded compute QBC size must remain deterministic")) {
        free(encoded);
        return 1;
    }

    QNBytecode decoded;
    memset(&decoded, 0, sizeof(decoded));
    memset(&diag, 0, sizeof(diag));
    status = qn_qbc_decode(
        encoded,
        encoded_size,
        &decoded,
        &diag
    );
    free(encoded);
    if (require_true(status == QN_OK,
                     "bounded compute QBC decode must succeed") ||
        require_true(
            qn_qbc_is_bounded_u32_vector_add(&decoded),
            "decoded bounded compute QBC must preserve contract")) {
        qn_bytecode_free(&decoded);
        return 1;
    }
    qn_bytecode_free(&decoded);

    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_route_qvm(
        QN_GPU_BACKEND_CPU,
        false,
        &bc,
        &route,
        &diag
    );
    if (require_true(status == QN_OK,
                     "default CPU bounded route must succeed") ||
        require_true(strcmp(route.selected_backend, "cpu") == 0,
                     "default bounded route must select CPU") ||
        require_true(strcmp(route.selection_reason, "default-cpu") == 0,
                     "default bounded route reason mismatch") ||
        require_true(strcmp(route.operation,
                            "bounded-uint32-vector-add") == 0,
                     "bounded operation name mismatch") ||
        require_true(route.gpu_eligible,
                     "bounded operation must be GPU eligible") ||
        require_true(!route.cpu_fallback,
                     "default CPU route is not a fallback")) {
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_route_qvm(
        QN_GPU_BACKEND_CPU,
        true,
        &bc,
        &route,
        &diag
    );
    if (require_true(status == QN_OK,
                     "explicit CPU bounded route must succeed") ||
        require_true(strcmp(route.selection_reason,
                            "explicit-cpu") == 0,
                     "explicit CPU bounded route reason mismatch")) {
        return 1;
    }

    bc.instructions[0].imm = QN_U32_VECTOR_ADD_COUNT - 1u;
    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_route_qvm(
        QN_GPU_BACKEND_CPU,
        true,
        &bc,
        &route,
        &diag
    );
    if (require_true(status == QN_ERR_RUNTIME,
                     "malformed bounded operation must fail closed") ||
        require_true(strcmp(diag.code, "QN-E7203") == 0,
                     "malformed bounded route diagnostic mismatch")) {
        return 1;
    }

    printf("PASS: QBIT_NOVA_BOUNDED_GPU_OPERATION_UNIT_V06_STEP5\n");
    return 0;
}
