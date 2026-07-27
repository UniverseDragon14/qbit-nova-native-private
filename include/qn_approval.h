#ifndef QN_APPROVAL_H
#define QN_APPROVAL_H

#include "qn_guard.h"

#define QN_APPROVAL_NONCE_CAP 65u
#define QN_APPROVAL_KEY_MIN 16u
#define QN_APPROVAL_KEY_MAX 4096u

typedef struct {
    uint32_t version;
    QNCapabilityMask capability;
    char capability_name[QN_NAME_CAP];
    uint8_t source_digest[32];
    uint64_t issued_at;
    uint64_t expires_at;
    char nonce[QN_APPROVAL_NONCE_CAP];
    uint8_t mac[32];
    uint8_t token_digest[32];
} QNApprovalToken;

uint8_t *qn_approval_load_key(const char *path,
                              size_t *key_size,
                              QNDiagnostic *diag);

QNStatus qn_approval_create(QNCapabilityMask capability,
                            const uint8_t source_digest[32],
                            uint64_t issued_at,
                            uint64_t expires_at,
                            const char *nonce,
                            const uint8_t *key,
                            size_t key_size,
                            QNApprovalToken *out,
                            QNDiagnostic *diag);

QNStatus qn_approval_write(const char *path,
                           const QNApprovalToken *token,
                           QNDiagnostic *diag);

QNStatus qn_approval_read(const char *path,
                          QNApprovalToken *out,
                          QNDiagnostic *diag);

QNStatus qn_approval_verify(const QNApprovalToken *token,
                            const uint8_t expected_source_digest[32],
                            uint64_t now,
                            const uint8_t *key,
                            size_t key_size,
                            QNCapabilityMask *approved,
                            QNDiagnostic *diag);

#endif
