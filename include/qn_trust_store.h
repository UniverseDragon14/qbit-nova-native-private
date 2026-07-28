#ifndef QN_TRUST_STORE_H
#define QN_TRUST_STORE_H

#include "qn_ed25519.h"

#define QN_TRUST_STORE_MAX_ISSUERS 32u
#define QN_TRUST_STORE_LABEL_MAX 64u

typedef struct {
    bool occupied;
    uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES];
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];
    char label[QN_TRUST_STORE_LABEL_MAX];
} QNTrustedIssuer;

typedef struct {
    QNTrustedIssuer issuers[QN_TRUST_STORE_MAX_ISSUERS];
    size_t count;
} QNTrustStore;

void qn_trust_store_init(QNTrustStore *store);

QNStatus qn_trust_store_add(
    QNTrustStore *store,
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    const char *label,
    QNDiagnostic *diag
);

bool qn_trust_store_contains(
    const QNTrustStore *store,
    const uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES]
);

QNStatus qn_trust_store_resolve(
    const QNTrustStore *store,
    const uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES],
    uint8_t public_key_out[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
);

#endif
