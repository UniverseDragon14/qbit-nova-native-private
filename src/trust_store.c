#include "qn_trust_store.h"

#include <stdio.h>
#include <string.h>

static bool constant_time_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
) {
    uint8_t difference = 0u;

    for (size_t index = 0u; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }

    return difference == 0u;
}

static bool public_key_is_zero(
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES]
) {
    uint8_t combined = 0u;

    for (size_t index = 0u;
         index < QN_ED25519_PUBLIC_KEY_BYTES;
         ++index) {
        combined |= public_key[index];
    }

    return combined == 0u;
}

static bool label_is_valid(const char *label) {
    if (!label) {
        return false;
    }

    for (size_t length = 0u;
         length < QN_TRUST_STORE_LABEL_MAX;
         ++length) {
        unsigned char character =
            (unsigned char)label[length];

        if (character == '\0') {
            return length != 0u;
        }

        if (character < 0x20u ||
            character > 0x7eu) {
            return false;
        }
    }

    return false;
}

static const QNTrustedIssuer *find_issuer(
    const QNTrustStore *store,
    const uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES]
) {
    if (!store || !fingerprint) {
        return NULL;
    }

    for (size_t index = 0u;
         index < QN_TRUST_STORE_MAX_ISSUERS;
         ++index) {
        const QNTrustedIssuer *issuer =
            &store->issuers[index];

        if (issuer->occupied &&
            constant_time_equal(
                issuer->fingerprint,
                fingerprint,
                QN_ED25519_FINGERPRINT_BYTES)) {
            return issuer;
        }
    }

    return NULL;
}

void qn_trust_store_init(QNTrustStore *store) {
    if (store) {
        memset(store, 0, sizeof(*store));
    }
}

QNStatus qn_trust_store_add(
    QNTrustStore *store,
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    const char *label,
    QNDiagnostic *diag
) {
    if (!store ||
        !public_key ||
        !label_is_valid(label) ||
        public_key_is_zero(public_key)) {
        qn_diag_set_code(
            diag,
            "QN-E5101",
            0,
            0,
            "invalid trusted issuer entry"
        );
        return QN_ERR_RUNTIME;
    }

    uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES];

    qn_ed25519_fingerprint(public_key, fingerprint);

    if (find_issuer(store, fingerprint)) {
        qn_diag_set_code(
            diag,
            "QN-E5102",
            0,
            0,
            "trusted issuer fingerprint already exists"
        );
        return QN_ERR_RUNTIME;
    }

    if (store->count >= QN_TRUST_STORE_MAX_ISSUERS) {
        qn_diag_set_code(
            diag,
            "QN-E5103",
            0,
            0,
            "trust store issuer limit reached"
        );
        return QN_ERR_LIMIT;
    }

    for (size_t index = 0u;
         index < QN_TRUST_STORE_MAX_ISSUERS;
         ++index) {
        QNTrustedIssuer *issuer = &store->issuers[index];

        if (!issuer->occupied) {
            memcpy(
                issuer->fingerprint,
                fingerprint,
                sizeof(issuer->fingerprint)
            );

            memcpy(
                issuer->public_key,
                public_key,
                sizeof(issuer->public_key)
            );

            snprintf(
                issuer->label,
                sizeof(issuer->label),
                "%s",
                label
            );

            issuer->occupied = true;
            ++store->count;
            return QN_OK;
        }
    }

    qn_diag_set_code(
        diag,
        "QN-E5103",
        0,
        0,
        "trust store has no available issuer slot"
    );

    return QN_ERR_LIMIT;
}

bool qn_trust_store_contains(
    const QNTrustStore *store,
    const uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES]
) {
    return find_issuer(store, fingerprint) != NULL;
}

QNStatus qn_trust_store_resolve(
    const QNTrustStore *store,
    const uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES],
    uint8_t public_key_out[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
) {
    if (!store || !fingerprint || !public_key_out) {
        qn_diag_set_code(
            diag,
            "QN-E5101",
            0,
            0,
            "invalid trust store lookup arguments"
        );
        return QN_ERR_RUNTIME;
    }

    const QNTrustedIssuer *issuer =
        find_issuer(store, fingerprint);

    if (!issuer) {
        qn_diag_set_code(
            diag,
            "QN-E5104",
            0,
            0,
            "approval issuer is not trusted"
        );
        return QN_ERR_RUNTIME;
    }

    memcpy(
        public_key_out,
        issuer->public_key,
        QN_ED25519_PUBLIC_KEY_BYTES
    );

    return QN_OK;
}
