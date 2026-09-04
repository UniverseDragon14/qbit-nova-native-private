#ifndef QN_VM_H
#define QN_VM_H

#include "qn_qbc.h"
#include "qn_gpu_routing.h"
#include "qn_device.h"

typedef struct {
    uint32_t values[QN_MAX_RUNTIME_INPUTS];
    uint16_t count;
    bool provided;
    uint8_t input_sha256[32];
} QNRuntimeInputs;

typedef struct {
    uint64_t state;
    uint64_t count;
} QNHistogramEntry;

typedef struct {
    uint32_t shots;
    uint64_t seed;
    uint32_t invalid_states;
    QNCapabilityMask approved_capabilities;
    bool has_approval_digest;
    uint8_t approval_digest[32];
    char approval_scheme[24];
    bool has_approval_issuer;
    uint8_t approval_issuer_fingerprint[32];
    bool approval_revocation_checked;
    bool approval_token_revoked;
    bool approval_issuer_revoked;
    bool approval_replay_consumed;
    char qvm_requested_backend[16];
    char qvm_selected_backend[16];
    char qvm_selection_reason[96];
    char qvm_operation[64];
    bool qvm_gpu_eligible;
    bool qvm_gpu_execution_attempted;
    bool qvm_gpu_execution_completed;
    bool qvm_cpu_fallback;
    bool native_compute_result;
    uint32_t compute_element_count;
    bool compute_cpu_reference_validated;
    bool compute_result_match;
    char compute_hardware_device[QN_GPU_DEVICE_NAME_CAP];
    uint32_t compute_hardware_vendor_id;
    uint8_t compute_shader_digest[32];
    uint8_t compute_output_digest[32];
    bool native_scalar_result;
    uint16_t scalar_output_id;
    uint32_t scalar_output_value;
    bool scalar_output_is_bool;
    uint8_t scalar_output_digest[32];
    bool runtime_inputs_provided;
    uint16_t runtime_input_abi;
    uint16_t runtime_input_count;
    uint8_t runtime_input_digest[32];
    bool native_device_result;
    QNDeviceResult device;
    QNHistogramEntry *entries;
    size_t entry_count;
    uint8_t qbc_digest[32];
} QNRunResult;

void qn_run_result_free(QNRunResult *result);
QNStatus qn_vm_run_guarded(const QNBytecode *bc,
                           uint32_t shots,
                           uint64_t seed,
                           const QNGuardPolicy *policy,
                           const QNGpuQvmRoute *route,
                           QNRunResult *out,
                           QNDiagnostic *diag);
QNStatus qn_vm_run_guarded_with_inputs(const QNBytecode *bc,
                                       uint32_t shots,
                                       uint64_t seed,
                                       const QNGuardPolicy *policy,
                                       const QNGpuQvmRoute *route,
                                       const QNRuntimeInputs *inputs,
                                       QNRunResult *out,
                                       QNDiagnostic *diag);
QNStatus qn_vm_run_guarded_with_device(const QNBytecode *bc,
                                       uint32_t shots,
                                       uint64_t seed,
                                       const QNGuardPolicy *policy,
                                       const QNGpuQvmRoute *route,
                                       const QNRuntimeInputs *inputs,
                                       const QNDeviceOptions *device_options,
                                       QNRunResult *out,
                                       QNDiagnostic *diag);
void qn_print_result(const QNBytecode *bc, const QNRunResult *result, FILE *stream);
QNStatus qn_write_receipt(const char *path, const QNBytecode *bc,
                          const QNRunResult *result, QNDiagnostic *diag);

#endif
