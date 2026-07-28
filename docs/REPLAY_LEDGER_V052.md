# QBIT NOVA Replay Ledger v0.5.2

The replay ledger uses the canonical text format `QNRL1`.

Grammar:

    QNRL1
    TOKEN_SHA256
    TOKEN_SHA256

Rules:

- The first line is exactly `QNRL1`.
- Each record is exactly 64 lowercase hexadecimal characters plus LF.
- Each digest is the SHA-256 digest of the complete signed approval token.
- The ledger contains at most 4096 records.
- New ledgers are created with mode `0600`.
- Existing ledgers with group or other permissions are rejected.
- Symbolic-link and hard-link ledger files are rejected.
- Reads use a shared POSIX advisory file lock.
- Consumption uses an exclusive POSIX advisory file lock.
- Check and append occur inside one exclusive critical section.
- The ledger file is synchronized with `fsync`.
- A newly created ledger also synchronizes its parent directory.
- Malformed, empty-existing, oversized, or unsupported ledgers fail closed.
- A missing ledger file is empty only when its parent directory exists.
- `qn_replay_ledger_contains` is informational and never mutates state.
- `qn_replay_ledger_consume` is the atomic replay-prevention primitive.
- A repeated token returns `QN-E5204` and does not append another record.
- The ledger must live in a trusted local directory. Advisory locks do not
  defend against an administrator replacing ancestor path components.

This step provides the persistent replay-ledger foundation only.
It does not yet wire token consumption into `run` or `exec`.
Execution-boundary integration is the next stage.
