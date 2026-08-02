#include "qn_gpu_compute.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    QNGpuComputeProof proof;
    QNDiagnostic diag;
    memset(&diag, 0, sizeof(diag));

    QNStatus status = qn_gpu_compute_proof(
        QN_GPU_BACKEND_CPU,
        &proof,
        &diag
    );

    if (status != QN_OK) {
        return fail("CPU proof returned an error");
    }
    if (strcmp(proof.selected_backend, "cpu") != 0) {
        return fail("CPU proof selected wrong backend");
    }
    if (strcmp(proof.selection_reason,
               "explicit-cpu-reference") != 0) {
        return fail("CPU proof selected wrong reason");
    }
    if (proof.gpu_execution_attempted ||
        proof.gpu_execution_completed) {
        return fail("CPU proof claimed GPU execution");
    }
    if (!proof.cpu_reference_validated ||
        !proof.result_match ||
        !proof.cpu_fallback) {
        return fail("CPU reference evidence is incomplete");
    }
    if (proof.element_count != QN_GPU_COMPUTE_ELEMENT_COUNT ||
        proof.local_size_x != QN_GPU_COMPUTE_LOCAL_SIZE_X ||
        proof.dispatch_x != QN_GPU_COMPUTE_DISPATCH_X) {
        return fail("bounded compute profile mismatch");
    }

    bool shader_nonzero = false;
    bool output_nonzero = false;
    for (size_t i = 0; i < 32u; ++i) {
        shader_nonzero = shader_nonzero ||
                         proof.shader_digest[i] != 0u;
        output_nonzero = output_nonzero ||
                         proof.output_digest[i] != 0u;
    }
    if (!shader_nonzero || !output_nonzero) {
        return fail("deterministic digest missing");
    }

    puts("PASS: QBIT_NOVA_GPU_COMPUTE_CPU_REFERENCE_V06_STEP3");
    return 0;
}
