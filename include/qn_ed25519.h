#ifndef QN_ED25519_H
#define QN_ED25519_H

#include "qn.h"

#define QN_ED25519_PRIVATE_KEY_BYTES 32u
#define QN_ED25519_PUBLIC_KEY_BYTES 32u
#define QN_ED25519_SIGNATURE_BYTES 64u
#define QN_ED25519_FINGERPRINT_BYTES 32u

QNStatus qn_ed25519_init(QNDiagnostic *diag);

QNStatus qn_ed25519_keypair_generate(
    uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
);

QNStatus qn_ed25519_public_from_private(
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
);

QNStatus qn_ed25519_sign(
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    const uint8_t *message,
    size_t message_size,
    uint8_t signature[QN_ED25519_SIGNATURE_BYTES],
    QNDiagnostic *diag
);

QNStatus qn_ed25519_verify(
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    const uint8_t *message,
    size_t message_size,
    const uint8_t signature[QN_ED25519_SIGNATURE_BYTES],
    QNDiagnostic *diag
);

void qn_ed25519_fingerprint(
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES]
);

void qn_ed25519_wipe_private(
    uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES]
);

#endif
