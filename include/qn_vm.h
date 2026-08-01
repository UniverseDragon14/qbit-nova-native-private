#ifndef QN_VM_H
#define QN_VM_H

#include "qn_qbc.h"

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
    QNHistogramEntry *entries;
    size_t entry_count;
    uint8_t qbc_digest[32];
} QNRunResult;

void qn_run_result_free(QNRunResult *result);
QNStatus qn_vm_run_guarded(const QNBytecode *bc,
                           uint32_t shots,
                           uint64_t seed,
                           const QNGuardPolicy *policy,
                           QNRunResult *out,
                           QNDiagnostic *diag);
void qn_print_result(const QNBytecode *bc, const QNRunResult *result, FILE *stream);
QNStatus qn_write_receipt(const char *path, const QNBytecode *bc,
                          const QNRunResult *result, QNDiagnostic *diag);

#endif
