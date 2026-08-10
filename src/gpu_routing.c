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

static bool qn_gpu_route_contains_compute_opcode(const QNBytecode *bc) {
    if (!bc || !bc->instructions) {
        return false;
    }

    for (size_t i = 0; i < bc->instruction_count; ++i) {
        if (bc->instructions[i].opcode == OP_U32_VECTOR_ADD) {
            return true;
        }
    }

    return false;
}

static QNStatus qn_gpu_route_bounded_compute(
    QNGpuBackendRequest requested,
    bool request_explicit,
    QNGpuQvmRoute *out,
    QNDiagnostic *diag
) {
    qn_gpu_route_set_text(
        out->operation,
        sizeof(out->operation),
        "bounded-uint32-vector-add"
    );
    out->gpu_eligible = true;

    if (requested == QN_GPU_BACKEND_CPU) {
        qn_gpu_route_set_text(
            out->selected_backend,
            sizeof(out->selected_backend),
            "cpu"
        );
        qn_gpu_route_set_text(
            out->selection_reason,
            sizeof(out->selection_reason),
            request_explicit ? "explicit-cpu" : "default-cpu"
        );
        out->cpu_fallback = false;
        return QN_OK;
    }

    if (requested != QN_GPU_BACKEND_AUTO &&
        requested != QN_GPU_BACKEND_VULKAN) {
        qn_diag_set_code(
            diag,
            "QN-E7200",
            0,
            0,
            "invalid QVM backend request"
        );
        return QN_ERR_RUNTIME;
    }

    QNGpuProbe probe;
    QNStatus probe_status = qn_gpu_probe(&probe, diag);

    if (probe_status != QN_OK ||
        !probe.hardware_candidate_available) {
        if (requested == QN_GPU_BACKEND_VULKAN) {
            qn_diag_set_code(
                diag,
                "QN-E7202",
                0,
                0,
                "bounded uint32 vector-add requires verified Broadcom V3D"
            );
            return QN_ERR_RUNTIME;
        }

        if (diag) {
            memset(diag, 0, sizeof(*diag));
        }
        qn_gpu_route_set_text(
            out->selected_backend,
            sizeof(out->selected_backend),
            "cpu"
        );
        qn_gpu_route_set_text(
            out->selection_reason,
            sizeof(out->selection_reason),
            "hardware-unavailable-cpu-fallback"
        );
        out->cpu_fallback = true;
        return QN_OK;
    }

    qn_gpu_route_set_text(
        out->selected_backend,
        sizeof(out->selected_backend),
        "vulkan"
    );
    qn_gpu_route_set_text(
        out->selection_reason,
        sizeof(out->selection_reason),
        requested == QN_GPU_BACKEND_AUTO
            ? "verified-v3d-auto"
            : "explicit-verified-v3d"
    );
    out->cpu_fallback = false;
    return QN_OK;
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

    bool contains_compute =
        qn_gpu_route_contains_compute_opcode(bc);

    if (contains_compute) {
        if (!qn_qbc_is_bounded_u32_vector_add(bc)) {
            qn_diag_set_code(
                diag,
                "QN-E7203",
                0,
                0,
                "malformed or mixed bounded GPU operation bytecode"
            );
            return QN_ERR_RUNTIME;
        }

        return qn_gpu_route_bounded_compute(
            requested,
            request_explicit,
            out,
            diag
        );
    }

    bool scalar_program = qn_qbc_is_typed_scalar_program(bc);
    bool pure_u32_scalar = qn_qbc_is_u32_scalar_program(bc);
    qn_gpu_route_set_text(
        out->operation,
        sizeof(out->operation),
        scalar_program
            ? (pure_u32_scalar
                ? "u32-scalar-program"
                : (qn_qbc_has_control_flow(bc)
                    ? "typed-control-flow-program"
                    : "typed-scalar-program"))
            : "quantum-state-simulation"
    );

    /*
     * Quantum-state simulation and scalar language execution remain
     * CPU-only. The fixed uint32 vector-add shader is never reused for
     * a different operation.
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
                scalar_program
                    ? "scalar-operation-not-gpu-eligible"
                    : "qvm-operation-not-gpu-eligible"
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
                "only bounded-uint32-vector-add is enabled",
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
