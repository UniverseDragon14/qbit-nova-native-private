#include "qn_ed25519.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#include <string.h>

static void set_openssl_error(QNDiagnostic *diag,
                              const char *code,
                              const char *operation) {
    unsigned long error_code = ERR_get_error();
    char detail[160];

    if (error_code != 0u) {
        ERR_error_string_n(error_code, detail, sizeof(detail));
    } else {
        snprintf(detail, sizeof(detail), "no OpenSSL error detail");
    }

    qn_diag_set_code(
        diag,
        code,
        0,
        0,
        "%s failed: %s",
        operation,
        detail
    );
}

QNStatus qn_ed25519_init(QNDiagnostic *diag) {
    if (OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL) != 1) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "OpenSSL initialization"
        );
        return QN_ERR_RUNTIME;
    }
    return QN_OK;
}

QNStatus qn_ed25519_keypair_generate(
    uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
) {
    if (!private_key || !public_key) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "keypair output pointers are required"
        );
        return QN_ERR_RUNTIME;
    }

    QNStatus status = qn_ed25519_init(diag);
    if (status != QN_OK) return status;

    EVP_PKEY_CTX *context =
        EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    EVP_PKEY *key = NULL;

    if (!context ||
        EVP_PKEY_keygen_init(context) != 1 ||
        EVP_PKEY_keygen(context, &key) != 1) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 key generation"
        );
        EVP_PKEY_free(key);
        EVP_PKEY_CTX_free(context);
        return QN_ERR_RUNTIME;
    }

    size_t private_size = QN_ED25519_PRIVATE_KEY_BYTES;
    size_t public_size = QN_ED25519_PUBLIC_KEY_BYTES;

    if (EVP_PKEY_get_raw_private_key(
            key,
            private_key,
            &private_size) != 1 ||
        private_size != QN_ED25519_PRIVATE_KEY_BYTES ||
        EVP_PKEY_get_raw_public_key(
            key,
            public_key,
            &public_size) != 1 ||
        public_size != QN_ED25519_PUBLIC_KEY_BYTES) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 raw-key export"
        );
        qn_ed25519_wipe_private(private_key);
        EVP_PKEY_free(key);
        EVP_PKEY_CTX_free(context);
        return QN_ERR_RUNTIME;
    }

    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(context);
    return QN_OK;
}

QNStatus qn_ed25519_public_from_private(
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
) {
    if (!private_key || !public_key) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "private and public key buffers are required"
        );
        return QN_ERR_RUNTIME;
    }

    QNStatus status = qn_ed25519_init(diag);
    if (status != QN_OK) return status;

    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        NULL,
        private_key,
        QN_ED25519_PRIVATE_KEY_BYTES
    );

    if (!key) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 private-key import"
        );
        return QN_ERR_RUNTIME;
    }

    size_t public_size = QN_ED25519_PUBLIC_KEY_BYTES;
    if (EVP_PKEY_get_raw_public_key(
            key,
            public_key,
            &public_size) != 1 ||
        public_size != QN_ED25519_PUBLIC_KEY_BYTES) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 public-key derivation"
        );
        EVP_PKEY_free(key);
        return QN_ERR_RUNTIME;
    }

    EVP_PKEY_free(key);
    return QN_OK;
}

QNStatus qn_ed25519_sign(
    const uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES],
    const uint8_t *message,
    size_t message_size,
    uint8_t signature[QN_ED25519_SIGNATURE_BYTES],
    QNDiagnostic *diag
) {
    if (!private_key ||
        !signature ||
        (message_size != 0u && !message)) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "invalid Ed25519 signing arguments"
        );
        return QN_ERR_RUNTIME;
    }

    QNStatus status = qn_ed25519_init(diag);
    if (status != QN_OK) return status;

    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        NULL,
        private_key,
        QN_ED25519_PRIVATE_KEY_BYTES
    );
    EVP_MD_CTX *context = EVP_MD_CTX_new();

    if (!key ||
        !context ||
        EVP_DigestSignInit(
            context,
            NULL,
            NULL,
            NULL,
            key) != 1) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 signing initialization"
        );
        EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);
        return QN_ERR_RUNTIME;
    }

    size_t signature_size = QN_ED25519_SIGNATURE_BYTES;
    if (EVP_DigestSign(
            context,
            signature,
            &signature_size,
            message,
            message_size) != 1 ||
        signature_size != QN_ED25519_SIGNATURE_BYTES) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 one-shot signing"
        );
        EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);
        return QN_ERR_RUNTIME;
    }

    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return QN_OK;
}

QNStatus qn_ed25519_verify(
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    const uint8_t *message,
    size_t message_size,
    const uint8_t signature[QN_ED25519_SIGNATURE_BYTES],
    QNDiagnostic *diag
) {
    if (!public_key ||
        !signature ||
        (message_size != 0u && !message)) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "invalid Ed25519 verification arguments"
        );
        return QN_ERR_RUNTIME;
    }

    QNStatus status = qn_ed25519_init(diag);
    if (status != QN_OK) return status;

    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519,
        NULL,
        public_key,
        QN_ED25519_PUBLIC_KEY_BYTES
    );
    EVP_MD_CTX *context = EVP_MD_CTX_new();

    if (!key ||
        !context ||
        EVP_DigestVerifyInit(
            context,
            NULL,
            NULL,
            NULL,
            key) != 1) {
        set_openssl_error(
            diag,
            "QN-E5001",
            "Ed25519 verification initialization"
        );
        EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);
        return QN_ERR_RUNTIME;
    }

    int verified = EVP_DigestVerify(
        context,
        signature,
        QN_ED25519_SIGNATURE_BYTES,
        message,
        message_size
    );

    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);

    if (verified != 1) {
        qn_diag_set_code(
            diag,
            "QN-E5002",
            0,
            0,
            "Ed25519 signature is invalid"
        );
        return QN_ERR_RUNTIME;
    }

    return QN_OK;
}

void qn_ed25519_fingerprint(
    const uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES],
    uint8_t fingerprint[QN_ED25519_FINGERPRINT_BYTES]
) {
    qn_sha256(
        public_key,
        QN_ED25519_PUBLIC_KEY_BYTES,
        fingerprint
    );
}

void qn_ed25519_wipe_private(
    uint8_t private_key[QN_ED25519_PRIVATE_KEY_BYTES]
) {
    if (private_key) {
        OPENSSL_cleanse(
            private_key,
            QN_ED25519_PRIVATE_KEY_BYTES
        );
    }
}
