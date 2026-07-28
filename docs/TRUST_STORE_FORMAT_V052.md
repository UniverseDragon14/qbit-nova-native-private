# QBIT NOVA Trust Store Format v0.5.2

The canonical text format is `QNTS1`.

Grammar:

    QNTS1
    issuer<TAB>PUBLIC_KEY_HEX<TAB>LABEL

Rules:

- The first line must be exactly `QNTS1`.
- Every line must end with LF (`\n`).
- CRLF is rejected.
- Blank lines and comments are rejected.
- Each issuer line begins with `issuer` followed by a tab.
- The public key is exactly 64 lowercase hexadecimal characters.
- The decoded public key is exactly 32 bytes.
- The all-zero public key is rejected.
- The label contains 1 to 63 printable ASCII characters.
- A trust-store file is limited to 4096 bytes.
- A trust store contains at most 32 issuers.
- Duplicate issuer fingerprints are rejected.
- Fingerprints are derived as SHA-256 of the raw public key.
- Parsing is transactional: output is replaced only after complete success.
- A header-only file is valid and represents an empty, deny-all trust store.

This stage does not provide CLI wiring, replay protection,
revocation, persistent updates, or execution-boundary consumption.
