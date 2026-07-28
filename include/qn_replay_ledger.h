#ifndef QN_REPLAY_LEDGER_H
#define QN_REPLAY_LEDGER_H

#include "qn.h"

#define QN_REPLAY_LEDGER_DIGEST_BYTES 32u
#define QN_REPLAY_LEDGER_MAX_RECORDS 4096u
#define QN_REPLAY_LEDGER_HEADER_BYTES 6u
#define QN_REPLAY_LEDGER_RECORD_BYTES 65u
#define QN_REPLAY_LEDGER_MAX_FILE_BYTES \
    (QN_REPLAY_LEDGER_HEADER_BYTES + \
     (QN_REPLAY_LEDGER_RECORD_BYTES * \
      QN_REPLAY_LEDGER_MAX_RECORDS))

QNStatus qn_replay_ledger_contains(
    const char *path,
    const uint8_t token_digest[QN_REPLAY_LEDGER_DIGEST_BYTES],
    bool *seen,
    QNDiagnostic *diag
);

QNStatus qn_replay_ledger_consume(
    const char *path,
    const uint8_t token_digest[QN_REPLAY_LEDGER_DIGEST_BYTES],
    bool *consumed,
    QNDiagnostic *diag
);

#endif
