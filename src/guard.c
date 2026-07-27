#include "qn_guard.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    QNCapabilityMask bit;
    const char *name;
} CapabilityName;

static const CapabilityName CAPABILITIES[] = {
    { QN_CAP_QUANTUM_SIMULATE, "quantum.simulate" },
    { QN_CAP_EVIDENCE_EMIT,    "evidence.emit" },
    { QN_CAP_MODEL_EXEC,       "model.exec" },
    { QN_CAP_FILE_WRITE,       "file.write" },
    { QN_CAP_NETWORK,          "network" },
    { QN_CAP_DEVICE_CONTROL,   "device.control" },
    { QN_CAP_SHELL_EXEC,       "shell.exec" }
};

static const size_t CAPABILITY_COUNT =
    sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]);

const char *qn_capability_name(QNCapabilityMask one_capability) {
    for (size_t i = 0; i < CAPABILITY_COUNT; ++i) {
        if (CAPABILITIES[i].bit == one_capability) {
            return CAPABILITIES[i].name;
        }
    }
    return "unknown";
}

bool qn_capability_parse(const char *name, QNCapabilityMask *out) {
    if (!name || !out) return false;

    for (size_t i = 0; i < CAPABILITY_COUNT; ++i) {
        if (strcmp(name, CAPABILITIES[i].name) == 0) {
            *out = CAPABILITIES[i].bit;
            return true;
        }
    }

    return false;
}

void qn_capability_format(QNCapabilityMask mask,
                          char *out,
                          size_t out_size) {
    if (!out || out_size == 0u) return;
    out[0] = '\0';

    bool first = true;
    for (size_t i = 0; i < CAPABILITY_COUNT; ++i) {
        if ((mask & CAPABILITIES[i].bit) == 0u) continue;

        size_t used = strlen(out);
        const char *separator = first ? "" : ",";
        int written = snprintf(
            out + used,
            out_size - used,
            "%s%s",
            separator,
            CAPABILITIES[i].name
        );

        if (written < 0 ||
            (size_t)written >= out_size - used) {
            out[out_size - 1u] = '\0';
            return;
        }

        first = false;
    }

    QNCapabilityMask unknown = mask & ~QN_CAP_KNOWN;
    if (unknown != 0u) {
        size_t used = strlen(out);
        snprintf(
            out + used,
            out_size - used,
            "%sunknown:0x%016llx",
            first ? "" : ",",
            (unsigned long long)unknown
        );
    }

    if (out[0] == '\0') {
        snprintf(out, out_size, "none");
    }
}

void qn_guard_policy_safe(QNGuardPolicy *policy) {
    memset(policy, 0, sizeof(*policy));
    snprintf(policy->profile, sizeof(policy->profile), "safe");
    policy->allowed = QN_CAP_SAFE_DEFAULT;
    policy->approval_required = QN_CAP_APPROVAL_DEFAULT;
    policy->blocked = QN_CAP_BLOCKED_DEFAULT;
}

void qn_guard_policy_deny_all(QNGuardPolicy *policy) {
    memset(policy, 0, sizeof(*policy));
    snprintf(policy->profile, sizeof(policy->profile), "deny-all");
    policy->blocked = QN_CAP_KNOWN;
}

bool qn_guard_policy_select(const char *name,
                            QNGuardPolicy *policy) {
    if (!name || !policy) return false;

    if (strcmp(name, "safe") == 0) {
        qn_guard_policy_safe(policy);
        return true;
    }

    if (strcmp(name, "deny-all") == 0) {
        qn_guard_policy_deny_all(policy);
        return true;
    }

    return false;
}

const char *qn_guard_status_name(QNGuardStatus status) {
    switch (status) {
        case QN_GUARD_ALLOWED: return "allowed";
        case QN_GUARD_NEEDS_APPROVAL: return "needs_approval";
        case QN_GUARD_BLOCKED: return "blocked";
        default: return "invalid";
    }
}

QNGuardDecision qn_guard_evaluate(
    QNCapabilityMask requested,
    const QNGuardPolicy *policy
) {
    QNGuardDecision decision;
    memset(&decision, 0, sizeof(decision));
    decision.requested = requested;

    decision.unknown = requested & ~QN_CAP_KNOWN;
    decision.blocked = requested & policy->blocked;

    QNCapabilityMask approval_candidates =
        requested & policy->approval_required;

    decision.missing_approval =
        approval_candidates & ~policy->approved;

    QNCapabilityMask classified =
        policy->allowed |
        policy->approval_required |
        policy->blocked;

    QNCapabilityMask unclassified =
        requested & ~classified;

    if (decision.unknown != 0u ||
        decision.blocked != 0u ||
        unclassified != 0u) {
        decision.status = QN_GUARD_BLOCKED;
        snprintf(
            decision.reason,
            sizeof(decision.reason),
            "deny-by-default: blocked, unknown or unclassified capability"
        );
        return decision;
    }

    if (decision.missing_approval != 0u) {
        decision.status = QN_GUARD_NEEDS_APPROVAL;
        snprintf(
            decision.reason,
            sizeof(decision.reason),
            "explicit approval required before execution"
        );
        return decision;
    }

    decision.status = QN_GUARD_ALLOWED;
    snprintf(
        decision.reason,
        sizeof(decision.reason),
        "all requested capabilities are allowed or explicitly approved"
    );
    return decision;
}

QNStatus qn_guard_enforce(QNCapabilityMask requested,
                          const QNGuardPolicy *policy,
                          QNDiagnostic *diag) {
    QNGuardDecision decision =
        qn_guard_evaluate(requested, policy);

    char requested_text[256];
    char missing_text[256];
    char blocked_text[256];
    char unknown_text[256];

    qn_capability_format(
        decision.requested,
        requested_text,
        sizeof(requested_text)
    );
    qn_capability_format(
        decision.missing_approval,
        missing_text,
        sizeof(missing_text)
    );
    qn_capability_format(
        decision.blocked,
        blocked_text,
        sizeof(blocked_text)
    );
    qn_capability_format(
        decision.unknown,
        unknown_text,
        sizeof(unknown_text)
    );

    if (decision.status == QN_GUARD_ALLOWED) {
        return QN_OK;
    }

    if (decision.status == QN_GUARD_NEEDS_APPROVAL) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-001",
            0,
            0,
            "capability approval required: requested=%s missing=%s",
            requested_text,
            missing_text
        );
        return QN_ERR_RUNTIME;
    }

    qn_diag_set_code(
        diag,
        "QN-E-GUARD-001",
        0,
        0,
        "capability blocked: requested=%s blocked=%s unknown=%s",
        requested_text,
        blocked_text,
        unknown_text
    );
    return QN_ERR_RUNTIME;
}
