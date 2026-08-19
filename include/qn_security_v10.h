#ifndef QN_SECURITY_V10_H
#define QN_SECURITY_V10_H

#include <stddef.h>
#include <stdint.h>

#define QN_SECURITY_ABI_V1 1u
#define QN_SECURITY_MAX_SCOPE_BYTES 256u

typedef enum {
    QN_SECURITY_CAP_SENSOR_RF_OBSERVE = 1,
    QN_SECURITY_CAP_SENSOR_CAMERA_DETECT = 2,
    QN_SECURITY_CAP_NETWORK_PASSIVE_OBSERVE = 3,

    QN_SECURITY_CAP_SCAN = 10,
    QN_SECURITY_CAP_AUDIT = 11,
    QN_SECURITY_CAP_FUZZ = 12,
    QN_SECURITY_CAP_VALIDATE = 13,
    QN_SECURITY_CAP_EXPLOIT_LAB = 14
} QNSecurityCapability;

typedef enum {
    QN_SECURITY_DECISION_DENY = 0,
    QN_SECURITY_DECISION_ALLOW = 1,
    QN_SECURITY_DECISION_NEEDS_APPROVAL = 2
} QNSecurityDecision;

enum {
    QN_SECURITY_FLAG_AUTHORIZED_SCOPE = 1u << 0,
    QN_SECURITY_FLAG_EXPLICIT_APPROVAL = 1u << 1,
    QN_SECURITY_FLAG_ISOLATED_LAB = 1u << 2,

    QN_SECURITY_FLAG_CREDENTIAL_THEFT = 1u << 16,
    QN_SECURITY_FLAG_PERSISTENCE = 1u << 17,
    QN_SECURITY_FLAG_DESTRUCTIVE = 1u << 18,
    QN_SECURITY_FLAG_COVERT_PERSON_TRACKING = 1u << 19
};

typedef struct {
    uint32_t abi_version;
    QNSecurityCapability capability;
    const char *scope;
    size_t scope_len;
    uint32_t flags;
} QNSecurityRequest;

typedef struct {
    QNSecurityDecision decision;
    const char *diagnostic_code;
    const char *reason;
} QNSecurityDecisionResult;

const char *qn_security_capability_name(QNSecurityCapability capability);

int qn_security_validate_request(
    const QNSecurityRequest *request,
    QNSecurityDecisionResult *result);

#endif
