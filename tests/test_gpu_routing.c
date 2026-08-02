#include "qn_gpu_routing.h"

#include <stdio.h>
#include <string.h>

static int require_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    QNBytecode bc;
    memset(&bc, 0, sizeof(bc));
    bc.total_qubits = 1u;
    bc.capability_mask = QN_CAP_QUANTUM_SIMULATE;

    QNDiagnostic diag;
    QNGpuQvmRoute route;
    QNStatus status;

    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_route_qvm(
        QN_GPU_BACKEND_CPU,
        false,
        &bc,
        &route,
        &diag
    );
    if (require_true(status == QN_OK,
                     "default CPU route must succeed") ||
        require_true(strcmp(route.selected_backend, "cpu") == 0,
                     "default route must select CPU") ||
        require_true(strcmp(route.selection_reason, "default-cpu") == 0,
                     "default route reason mismatch") ||
        require_true(!route.cpu_fallback,
                     "default CPU is not a fallback") ||
        require_true(!route.gpu_eligible,
                     "QVM operation must not be GPU eligible")) {
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
                     "explicit CPU route must succeed") ||
        require_true(strcmp(route.selection_reason, "explicit-cpu") == 0,
                     "explicit CPU route reason mismatch") ||
        require_true(!route.cpu_fallback,
                     "explicit CPU must not be a fallback")) {
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_route_qvm(
        QN_GPU_BACKEND_AUTO,
        true,
        &bc,
        &route,
        &diag
    );
    if (require_true(status == QN_OK,
                     "auto route must safely fall back") ||
        require_true(strcmp(route.selected_backend, "cpu") == 0,
                     "auto route must select CPU") ||
        require_true(
            strcmp(
                route.selection_reason,
                "qvm-operation-not-gpu-eligible"
            ) == 0,
            "auto fallback reason mismatch"
        ) ||
        require_true(route.cpu_fallback,
                     "auto route must record CPU fallback") ||
        require_true(!route.gpu_execution_attempted,
                     "auto route must not claim GPU attempt") ||
        require_true(!route.gpu_execution_completed,
                     "auto route must not claim GPU completion")) {
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_route_qvm(
        QN_GPU_BACKEND_VULKAN,
        true,
        &bc,
        &route,
        &diag
    );
    if (require_true(status == QN_ERR_RUNTIME,
                     "explicit Vulkan QVM route must fail closed") ||
        require_true(strcmp(diag.code, "QN-E7201") == 0,
                     "explicit Vulkan diagnostic mismatch")) {
        return 1;
    }

    printf("PASS: QBIT_NOVA_QVM_GPU_ROUTING_UNIT_V06_STEP4\n");
    return 0;
}
