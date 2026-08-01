#include "qn_gpu_adapter.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    QNGpuDeviceCandidate candidate;
    memset(&candidate, 0, sizeof(candidate));
    snprintf(candidate.name, sizeof(candidate.name), "V3D 7.1.7.0");
    candidate.vendor_id = 0x14e4u;
    candidate.device_id = 0x55701c33u;
    candidate.device_type = 1u;
    candidate.compute_queue = true;
    candidate.render_access = true;

    if (!qn_gpu_candidate_is_supported(&candidate)) {
        return fail("strict V3D candidate rejected");
    }

    snprintf(candidate.name, sizeof(candidate.name),
             "llvmpipe (LLVM 19.1.7, 128 bits)");
    candidate.vendor_id = 0x10005u;
    candidate.device_type = 4u;
    if (qn_gpu_candidate_is_supported(&candidate)) {
        return fail("llvmpipe accepted as hardware GPU");
    }

    snprintf(candidate.name, sizeof(candidate.name), "V3D 7.1.7.0");
    candidate.vendor_id = 0x14e4u;
    candidate.device_type = 1u;
    candidate.compute_queue = false;
    candidate.render_access = true;
    if (qn_gpu_candidate_is_supported(&candidate)) {
        return fail("candidate without compute queue accepted");
    }

    candidate.compute_queue = true;
    candidate.render_access = false;
    if (qn_gpu_candidate_is_supported(&candidate)) {
        return fail("candidate without render access accepted");
    }

    QNGpuBackendRequest backend;
    if (!qn_gpu_backend_parse("auto", &backend) ||
        backend != QN_GPU_BACKEND_AUTO) {
        return fail("auto backend parse failed");
    }
    if (!qn_gpu_backend_parse("cpu", &backend) ||
        backend != QN_GPU_BACKEND_CPU) {
        return fail("cpu backend parse failed");
    }
    if (!qn_gpu_backend_parse("vulkan", &backend) ||
        backend != QN_GPU_BACKEND_VULKAN) {
        return fail("vulkan backend parse failed");
    }
    if (qn_gpu_backend_parse("llvmpipe", &backend)) {
        return fail("unknown backend accepted");
    }

    QNGpuProbe probe;
    memset(&probe, 0, sizeof(probe));
    probe.hardware_candidate_available = true;

    QNGpuDecision decision;
    QNDiagnostic diag;
    memset(&diag, 0, sizeof(diag));

    QNStatus status = qn_gpu_decide(
        QN_GPU_BACKEND_AUTO,
        &probe,
        &decision,
        &diag
    );
    if (status != QN_OK ||
        strcmp(decision.selected_backend, "cpu") != 0 ||
        strcmp(decision.selection_reason,
               "gpu-kernel-not-implemented") != 0 ||
        decision.gpu_execution_enabled ||
        !decision.cpu_fallback) {
        return fail("auto CPU fallback contract failed");
    }

    memset(&diag, 0, sizeof(diag));
    status = qn_gpu_decide(
        QN_GPU_BACKEND_VULKAN,
        &probe,
        &decision,
        &diag
    );
    if (status != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E7005") != 0) {
        return fail("Vulkan execution was not denied");
    }

    puts("PASS: QBIT_NOVA_GPU_ADAPTER_CONTRACT_V06_STEP2");
    return 0;
}
