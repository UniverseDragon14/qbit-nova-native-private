#ifndef QN_REVOCATION_STORE_H
#define QN_REVOCATION_STORE_H

#include "qn.h"

#define QN_REVOCATION_DIGEST_BYTES 32u
#define QN_REVOCATION_REASON_MAX 64u
#define QN_REVOCATION_MAX_ENTRIES 64u
#define QN_REVOCATION_MAX_FILE_BYTES 16384u

typedef enum {
    QN_REVOCATION_TOKEN = 1,
    QN_REVOCATION_ISSUER = 2
} QNRevocationKind;

typedef struct {
    bool occupied;
    QNRevocationKind kind;
    uint8_t digest[QN_REVOCATION_DIGEST_BYTES];
    char reason[QN_REVOCATION_REASON_MAX];
} QNRevocationEntry;

typedef struct {
    QNRevocationEntry entries[QN_REVOCATION_MAX_ENTRIES];
    size_t count;
} QNRevocationStore;

void qn_revocation_store_init(QNRevocationStore *store);

QNStatus qn_revocation_store_parse_text(
    const uint8_t *data,
    size_t size,
    QNRevocationStore *out,
    QNDiagnostic *diag
);

QNStatus qn_revocation_store_load_file(
    const char *path,
    QNRevocationStore *out,
    QNDiagnostic *diag
);

bool qn_revocation_store_token_is_revoked(
    const QNRevocationStore *store,
    const uint8_t token_digest[QN_REVOCATION_DIGEST_BYTES]
);

bool qn_revocation_store_issuer_is_revoked(
    const QNRevocationStore *store,
    const uint8_t issuer_fingerprint[QN_REVOCATION_DIGEST_BYTES]
);

QNStatus qn_revocation_store_check(
    const QNRevocationStore *store,
    const uint8_t token_digest[QN_REVOCATION_DIGEST_BYTES],
    const uint8_t issuer_fingerprint[QN_REVOCATION_DIGEST_BYTES],
    QNDiagnostic *diag
);

#endif
