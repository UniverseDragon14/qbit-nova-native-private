# Authenticated Approval v0.4

## Format

```text
QBIT_NOVA_APPROVAL_V04
version=1
capability=model.exec
source_sha256=<64 lowercase hex characters>
issued_at=<Unix time>
expires_at=<Unix time>
nonce=<1..64 safe characters>
mac_sha256=<HMAC-SHA-256>
```

The MAC covers every preceding line, including the marker.

## Validation order

1. Strict token format
2. Known approval-eligible capability
3. Valid activation and expiry interval
4. Exact source SHA-256 scope match
5. Constant-time HMAC comparison
6. Capability guard evaluation
7. QVM execution

## Deterministic diagnostics

- `QN-E-APPROVAL-002`: invalid or inactive time scope
- `QN-E-APPROVAL-003`: source scope mismatch
- `QN-E-APPROVAL-004`: authentication or tamper failure
- `QN-E-APPROVAL-005`: expired token
- `QN-E-APPROVAL-006`: capability is not approval-eligible
- `QN-E-APPROVAL-007`: invalid key size
- `QN-E-APPROVAL-008`: internal token size limit
- `QN-E-APPROVAL-009`: invalid nonce
- `QN-E-APPROVAL-010`: malformed token
- `QN-E-APPROVAL-011`: incomplete execution approval options
- `QN-E-APPROVAL-012`: invalid approval CLI usage
- `QN-E-CAP-DECL-001`: unknown source capability declaration

## Security notes

- The shared key must remain secret.
- HMAC approval is not publicly verifiable.
- v0.4 does not implement revocation storage or replay tracking.
- A token authorizes only the capability and exact source digest it names.
- A token cannot convert a blocked capability such as `shell.exec` into an
  allowed capability.
