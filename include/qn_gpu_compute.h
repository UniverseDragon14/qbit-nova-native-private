#ifndef QN_GPU_COMPUTE_H
#define QN_GPU_COMPUTE_H

#include "qn_gpu_adapter.h"

#define QN_GPU_COMPUTE_ELEMENT_COUNT 256u
#define QN_GPU_COMPUTE_LOCAL_SIZE_X 64u
#define QN_GPU_COMPUTE_DISPATCH_X 4u

typedef struct {
    QNGpuBackendRequest requested;
    const char *selected_backend;
    const char *selection_reason;
    bool gpu_execution_attempted;
    bool gpu_execution_completed;
    bool cpu_reference_validated;
    bool result_match;
    bool cpu_fallback;
    char hardware_device[QN_GPU_DEVICE_NAME_CAP];
    uint32_t hardware_vendor_id;
    uint32_t element_count;
    uint32_t local_size_x;
    uint32_t dispatch_x;
    uint8_t shader_digest[32];
    uint8_t output_digest[32];
} QNGpuComputeProof;

QNStatus qn_gpu_compute_proof(QNGpuBackendRequest requested,
                              QNGpuComputeProof *out,
                              QNDiagnostic *diag);
void qn_gpu_compute_print(const QNGpuComputeProof *proof, FILE *stream);
QNStatus qn_gpu_compute_write_receipt(const char *path,
                                      const QNGpuComputeProof *proof,
                                      QNDiagnostic *diag);

#endif
