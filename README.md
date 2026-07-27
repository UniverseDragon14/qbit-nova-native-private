# QBIT NOVA Native v0.4

A clean-room C17 programming-language foundation by Universal Dragon Aslam.

## Stage 4 completed

```text
.qn source
  -> lexer
  -> AST
  -> typed QIR
  -> capability declaration and derivation
  -> QBC v3
  -> HMAC-SHA-256 approval verification
  -> expiry and source-scope validation
  -> deny-by-default guard
  -> native QVM
  -> evidence receipt
```

## New language declaration

```qn
requires model.exec
```

The declaration does not execute a model by itself. It marks the compiled
program as requiring that authority. A later model backend must still implement
the operation.

## Approval token

v0.4 uses an HMAC-SHA-256 authenticated approval token. It provides tamper
detection when the verifier protects the shared key.

This is a keyed authentication mechanism, not a public-key digital signature.
Publicly verifiable Ed25519 signatures remain a later stage.

The token is bound to:

- one approval-eligible capability
- the exact source SHA-256
- issue time
- expiry time
- nonce

## Key creation

Create a private key file and protect it:

```bash
umask 077
head -c 32 /dev/urandom > approval.key
chmod 600 approval.key
```

Never commit the key.

## Issue an approval

```bash
./build/qnova approval issue \
  examples/approval_model.qn \
  model.exec \
  --key-file approval.key \
  --expires-at 2000003600 \
  -o model.qna
```

## Verify

```bash
./build/qnova approval verify \
  examples/approval_model.qn \
  model.qna \
  --key-file approval.key
```

## Guarded execution

```bash
./build/qnova run examples/approval_model.qn \
  --approval-file model.qna \
  --approval-key-file approval.key \
  --receipt build/model-receipt.json
```

Bare `--approve` is not accepted for `run` or `exec`. Approval-required
execution must present an authenticated token.

## Default policy

| Capability | Decision |
|---|---|
| `quantum.simulate` | allowed |
| `evidence.emit` | allowed |
| `model.exec` | authenticated approval required |
| `file.write` | authenticated approval required |
| `network` | authenticated approval required |
| `device.control` | authenticated approval required |
| `shell.exec` | blocked |

Unknown authority remains denied by default.

## Scientific boundary

This remains a software virtual QCPU. It is not physical quantum hardware.

## Project separation

QBIT NOVA C remains frozen and untouched.
