#ifndef QN_GPU_ADAPTER_H
#define QN_GPU_ADAPTER_H

#include "qn.h"

#define QN_GPU_DEVICE_NAME_CAP 256u
#define QN_GPU_RENDER_NODE "/dev/dri/renderD128"

#define QN_GPU_PROFILE_MAX_WORKGROUP_INVOCATIONS 256u
#define QN_GPU_PROFILE_MAX_COMPUTE_SHARED_MEMORY 16384u
#define QN_GPU_PROFILE_SUBGROUP_SIZE 16u

typedef enum {
    QN_GPU_BACKEND_AUTO = 0,
    QN_GPU_BACKEND_CPU,
    QN_GPU_BACKEND_VULKAN
} QNGpuBackendRequest;

typedef struct {
    char name[QN_GPU_DEVICE_NAME_CAP];
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t device_type;
    bool compute_queue;
    bool render_access;
} QNGpuDeviceCandidate;

typedef struct {
    bool vulkan_loader_available;
    bool render_node_readable;
    bool render_node_writable;
    bool llvmpipe_seen;
    bool hardware_candidate_available;
    QNGpuDeviceCandidate hardware_candidate;
} QNGpuProbe;

typedef struct {
    QNGpuBackendRequest requested;
    const char *selected_backend;
    const char *selection_reason;
    bool gpu_execution_enabled;
    bool cpu_fallback;
} QNGpuDecision;

bool qn_gpu_backend_parse(const char *name, QNGpuBackendRequest *out);
const char *qn_gpu_backend_name(QNGpuBackendRequest backend);
bool qn_gpu_candidate_is_supported(const QNGpuDeviceCandidate *candidate);
QNStatus qn_gpu_probe(QNGpuProbe *out, QNDiagnostic *diag);
QNStatus qn_gpu_decide(QNGpuBackendRequest requested,
                       const QNGpuProbe *probe,
                       QNGpuDecision *out,
                       QNDiagnostic *diag);
void qn_gpu_print_contract(const QNGpuProbe *probe,
                           const QNGpuDecision *decision,
                           FILE *stream);
QNStatus qn_gpu_write_receipt(const char *path,
                              const QNGpuProbe *probe,
                              const QNGpuDecision *decision,
                              QNDiagnostic *diag);

#endif
