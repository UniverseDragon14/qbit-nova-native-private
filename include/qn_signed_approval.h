#ifndef QN_SIGNED_APPROVAL_H
#define QN_SIGNED_APPROVAL_H

#include "qn_ed25519.h"
#include "qn_guard.h"

#define QN_SIGNED_APPROVAL_CONTEXT_MAX 256u
#define QN_SIGNED_APPROVAL_NONCE_BYTES 16u
#define QN_SIGNED_APPROVAL_DOMAIN_BYTES 32u

typedef struct {
    uint32_t capability_id;
    QNCapabilityMask capability;
    uint8_t issuer_fingerprint[32];
    uint8_t source_digest[32];
    uint64_t issued_at;
    uint64_t expires_at;
    uint8_t nonce[QN_SIGNED_APPROVAL_NONCE_BYTES];
    uint16_t context_size;
    uint8_t context[QN_SIGNED_APPROVAL_CONTEXT_MAX];
    uint8_t signature[QN_ED25519_SIGNATURE_BYTES];
    uint8_t token_digest[32];
} QNSignedApprovalToken;

bool qn_capability_to_stable_id(
    QNCapabilityMask capability,
    uint32_t *stable_id
);

bool qn_capability_from_stable_id(
    uint32_t stable_id,
    QNCapabilityMask *capability
);

uint8_t *qn_signed_approval_load_key(
    const char *path,
    size_t expected_size,
    QNDiagnostic *diag
);

QNStatus qn_signed_approval_keypair_generate(
    const char *private_path,
    const char *public_path,
    QNDiagnostic *diag
);

QNStatus qn_signed_approval_derive_public(
    const char *private_path,
    const char *public_path,
    QNDiagnostic *diag
);

bool qn_signed_approval_parse_nonce_hex(
    const char *text,
    uint8_t nonce[QN_SIGNED_APPROVAL_NONCE_BYTES]
);

QNStatus qn_signed_approval_issue(
    QNCapabilityMask capability,
    QNCapabilityMask program_capabilities,
    const uint8_t source_digest[32],
    uint64_t issued_at,
    uint64_t expires_at,
    const uint8_t *nonce_or_null,
    const uint8_t *context,
    size_t context_size,
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    QNSignedApprovalToken *out,
    QNDiagnostic *diag
);

QNStatus qn_signed_approval_write(
    const char *path,
    const QNSignedApprovalToken *token,
    QNDiagnostic *diag
);

QNStatus qn_signed_approval_read(
    const char *path,
    QNSignedApprovalToken *out,
    QNDiagnostic *diag
);

QNStatus qn_signed_approval_verify(
    const QNSignedApprovalToken *token,
    const uint8_t expected_source_digest[32],
    QNCapabilityMask program_capabilities,
    uint64_t now,
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    QNCapabilityMask *approved,
    QNDiagnostic *diag
);

#endif
