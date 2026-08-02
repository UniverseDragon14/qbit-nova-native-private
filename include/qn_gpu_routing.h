#ifndef QN_GPU_ROUTING_H
#define QN_GPU_ROUTING_H

#include "qn_gpu_adapter.h"
#include "qn_qbc.h"

#define QN_GPU_ROUTE_BACKEND_CAP 16u
#define QN_GPU_ROUTE_REASON_CAP 96u
#define QN_GPU_ROUTE_OPERATION_CAP 64u

typedef struct {
    QNGpuBackendRequest requested;
    char selected_backend[QN_GPU_ROUTE_BACKEND_CAP];
    char selection_reason[QN_GPU_ROUTE_REASON_CAP];
    char operation[QN_GPU_ROUTE_OPERATION_CAP];
    bool gpu_eligible;
    bool gpu_execution_attempted;
    bool gpu_execution_completed;
    bool cpu_fallback;
} QNGpuQvmRoute;

QNStatus qn_gpu_route_qvm(QNGpuBackendRequest requested,
                          bool request_explicit,
                          const QNBytecode *bc,
                          QNGpuQvmRoute *out,
                          QNDiagnostic *diag);

#endif
