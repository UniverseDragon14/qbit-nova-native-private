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

This step provides the strict revocation-store foundation only.
It does not yet wire revocation checks into approval verification,
`run`, `exec`, replay consumption, or deterministic receipts.
