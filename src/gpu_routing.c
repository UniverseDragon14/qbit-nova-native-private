#include "qn_gpu_routing.h"

#include <stdio.h>
#include <string.h>

static void qn_gpu_route_set_text(char *out,
                                  size_t out_size,
                                  const char *value) {
    if (!out || out_size == 0u) {
        return;
    }

    snprintf(out, out_size, "%s", value ? value : "");
}

QNStatus qn_gpu_route_qvm(QNGpuBackendRequest requested,
                          bool request_explicit,
                          const QNBytecode *bc,
                          QNGpuQvmRoute *out,
                          QNDiagnostic *diag) {
    if (!bc || !out) {
        qn_diag_set_code(
            diag,
            "QN-E7200",
            0,
            0,
            "invalid QVM GPU routing arguments"
        );
        return QN_ERR_RUNTIME;
    }

    memset(out, 0, sizeof(*out));
    out->requested = requested;

    qn_gpu_route_set_text(
        out->operation,
        sizeof(out->operation),
        "quantum-state-simulation"
    );

    /*
     * Stage 6 Step 4 deliberately does not map QBC quantum-state
     * simulation onto the bounded uint32 vector-add proof kernel.
     * A successful route must never imply GPU execution that did
     * not happen.
     */
    out->gpu_eligible = false;
    out->gpu_execution_attempted = false;
    out->gpu_execution_completed = false;

    switch (requested) {
        case QN_GPU_BACKEND_CPU:
            qn_gpu_route_set_text(
                out->selected_backend,
                sizeof(out->selected_backend),
                "cpu"
            );
            qn_gpu_route_set_text(
                out->selection_reason,
                sizeof(out->selection_reason),
                request_explicit
                    ? "explicit-cpu"
                    : "default-cpu"
            );
            out->cpu_fallback = false;
            return QN_OK;

        case QN_GPU_BACKEND_AUTO:
            qn_gpu_route_set_text(
                out->selected_backend,
                sizeof(out->selected_backend),
                "cpu"
            );
            qn_gpu_route_set_text(
                out->selection_reason,
                sizeof(out->selection_reason),
                "qvm-operation-not-gpu-eligible"
            );
            out->cpu_fallback = true;
            return QN_OK;

        case QN_GPU_BACKEND_VULKAN:
            qn_diag_set_code(
                diag,
                "QN-E7201",
                0,
                0,
                "QVM operation '%s' is not GPU-eligible; "
                "only the bounded gpu compute-proof "
                "uint32-vector-add kernel is enabled",
                out->operation
            );
            return QN_ERR_RUNTIME;

        default:
            qn_diag_set_code(
                diag,
                "QN-E7200",
                0,
                0,
                "invalid QVM backend request"
            );
            return QN_ERR_RUNTIME;
    }
}
