# QBIT NOVA Revocation Store v0.5.2

The canonical revocation format is `QNRV1`.

Grammar:

    QNRV1
    token<TAB>TOKEN_SHA256<TAB>REASON
    issuer<TAB>ISSUER_FINGERPRINT<TAB>REASON

Rules:

- The first line is exactly `QNRV1`.
- Every line ends with LF.
- CRLF, blank lines, comments, NUL bytes, and unknown record types are rejected.
- Token digests and issuer fingerprints are exactly 64 lowercase hexadecimal characters.
- All-zero digests are rejected.
- Reasons contain 1 to 63 printable ASCII characters.
- A store contains at most 64 entries.
- The file is limited to 16384 bytes.
- Duplicate entries in the same namespace are rejected.
- Parsing is transactional: output is replaced only after complete success.
- A header-only file is valid and represents an empty revocation set.
- Missing files fail closed with `QN-E6106`.
- Symbolic links, hard links, non-regular files, and files writable by group or others are rejected.
- Token revocation returns `QN-E6107`.
- Issuer revocation returns `QN-E6108`.
- Token revocation is checked before issuer revocation.

Execution-boundary integration:

- Ed25519 `run` and `exec` require an explicit
  `--revocation-store-file`.
- Signature verification and trusted-key resolution complete before
  revocation lookup.
- Token and issuer revocation checks complete before runtime
  preflight and before replay-ledger consumption.
- A revoked token returns `QN-E6107` without creating or modifying
  the replay ledger.
- A revoked issuer returns `QN-E6108` without creating or modifying
  the replay ledger.
- Missing or unsafe revocation files fail closed with `QN-E6106`.
- A missing CLI pairing fails closed with `QN-E6109`.
- `approval verify-ed25519` remains non-mutating and does not consume
  replay state.
- HMAC v0.4 remains a legacy compatibility path outside this
  Ed25519 revocation requirement.

Successful Ed25519 `run` and `exec` output now records:

- `approval_revocation=checked-clear`
- `approval_token_revoked=false`
- `approval_issuer_revoked=false`
- `approval_replay=consumed`

Deterministic JSON receipts record the same evidence with boolean
token and issuer fields. HMAC and unsigned execution record
`not-applicable`, with JSON `null` for token and issuer revocation.
