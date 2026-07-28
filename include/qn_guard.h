#ifndef QN_GUARD_H
#define QN_GUARD_H

#include "qn.h"

typedef uint64_t QNCapabilityMask;

enum {
    QN_CAP_QUANTUM_SIMULATE = UINT64_C(1) << 0,
    QN_CAP_EVIDENCE_EMIT    = UINT64_C(1) << 1,

    QN_CAP_MODEL_EXEC       = UINT64_C(1) << 8,
    QN_CAP_FILE_WRITE       = UINT64_C(1) << 9,
    QN_CAP_NETWORK          = UINT64_C(1) << 10,
    QN_CAP_DEVICE_CONTROL   = UINT64_C(1) << 11,

    QN_CAP_SHELL_EXEC       = UINT64_C(1) << 16
};

#define QN_CAP_SAFE_DEFAULT \
    (QN_CAP_QUANTUM_SIMULATE | QN_CAP_EVIDENCE_EMIT)

#define QN_CAP_APPROVAL_DEFAULT \
    (QN_CAP_MODEL_EXEC | QN_CAP_FILE_WRITE | \
     QN_CAP_NETWORK | QN_CAP_DEVICE_CONTROL)

#define QN_CAP_BLOCKED_DEFAULT \
    (QN_CAP_SHELL_EXEC)

#define QN_CAP_KNOWN \
    (QN_CAP_SAFE_DEFAULT | QN_CAP_APPROVAL_DEFAULT | \
     QN_CAP_BLOCKED_DEFAULT)

typedef enum {
    QN_GUARD_ALLOWED = 0,
    QN_GUARD_NEEDS_APPROVAL = 1,
    QN_GUARD_BLOCKED = 2
} QNGuardStatus;

typedef struct {
    char profile[32];
    QNCapabilityMask allowed;
    QNCapabilityMask approval_required;
    QNCapabilityMask blocked;
    QNCapabilityMask approved;
    bool has_approval_digest;
    uint8_t approval_digest[32];
    char approval_scheme[24];
    bool has_approval_issuer;
    uint8_t approval_issuer_fingerprint[32];
} QNGuardPolicy;

typedef struct {
    QNGuardStatus status;
    QNCapabilityMask requested;
    QNCapabilityMask missing_approval;
    QNCapabilityMask blocked;
    QNCapabilityMask unknown;
    char reason[192];
} QNGuardDecision;

const char *qn_capability_name(QNCapabilityMask one_capability);
bool qn_capability_parse(const char *name, QNCapabilityMask *out);
void qn_capability_format(QNCapabilityMask mask, char *out, size_t out_size);

void qn_guard_policy_safe(QNGuardPolicy *policy);
void qn_guard_policy_deny_all(QNGuardPolicy *policy);
bool qn_guard_policy_select(const char *name, QNGuardPolicy *policy);

QNGuardDecision qn_guard_evaluate(QNCapabilityMask requested,
                                  const QNGuardPolicy *policy);

QNStatus qn_guard_enforce(QNCapabilityMask requested,
                          const QNGuardPolicy *policy,
                          QNDiagnostic *diag);

const char *qn_guard_status_name(QNGuardStatus status);

#endif
