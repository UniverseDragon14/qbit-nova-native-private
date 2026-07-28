# Ed25519 Approval v0.5

## Wire format

All integers are unsigned big-endian.

```text
magic                  5 bytes   "QNAT1"
domain                32 bytes   QBIT-NOVA-APPROVAL-ED25519-V05 + zero padding
issuer_fingerprint    32 bytes   SHA-256(raw public key)
capability_id          4 bytes   stable versioned ID
source_digest         32 bytes   SHA-256(exact source bytes)
issued_at              8 bytes   Unix seconds
expires_at             8 bytes   Unix seconds
nonce                  16 bytes  CSPRNG or explicit test fixture
context_len            2 bytes   0..256
context                N bytes
signature              64 bytes  Ed25519 over every preceding byte
```

## Verification order

1. Strict size, magic, domain and context-length parsing
2. Stable capability-ID decoding
3. Trusted public-key fingerprint match
4. Ed25519 signature verification
5. Approval-eligibility and blocked-capability check
6. Timestamp validation
7. Exact source-digest binding
8. Program capability-scope validation
9. Deny-by-default guard
10. QVM execution and receipt generation

## Diagnostics

- `QN-E5001` crypto initialization or operation failed
- `QN-E5002` signature invalid
- `QN-E5003` issuer public key not trusted
- `QN-E5004` malformed token, key or invalid fields
- `QN-E5005` token version/domain unsupported
- `QN-E5006` token expired
- `QN-E5007` token not yet valid
- `QN-E5008` source scope mismatch
- `QN-E5009` capability ID/scope mismatch
- `QN-E5010` blocked capability cannot be approved
- `QN-E5012` context oversized

`QN-E5011` is deliberately unused because replay/revocation storage is not
implemented in v0.5. The nonce is present for future ledgers but is not a replay
guarantee by itself.
