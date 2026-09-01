# QBIT NOVA Native — Stage 6 Pi5 GPU Adapter

> Experimental checkpoint branch; not a production release.

## Branch checkpoint

- Branch: `stage6-pi5-gpu-adapter`
- Inspected code tip: `8d7c7c663fe5`
- Focus: Adds CPU/Vulkan routing and a bounded native V3D compute pipeline. GPU work is classical compute, not physical quantum execution.
- Full repository branch inventory: [audited branch map](https://github.com/UniverseDragon14/qbit-nova-native-private/blob/stage7-step9-native-tensor-memory/BRANCHES.md)

## Truth boundary

`boundary=software_virtual_qcpu,native_bounded_compute`  
`physical_qpu=false`

The Raspberry Pi 5 is the classical host. CPU/Vulkan execution, qbits and state-vector behavior are software paths.

Native C17 language foundation by Universal Dragon Aslam.

## Stage 5

v0.5 adds OpenSSL-backed Ed25519 public-key approvals while preserving the
v0.4 HMAC approval format for compatibility.

```text
.qn source
  -> lexer
  -> AST
  -> typed QIR
  -> capability metadata
  -> QBC
  -> Ed25519 signed approval verification
  -> deny-by-default guard
  -> native QVM
  -> evidence receipt
```

## Why OpenSSL

The Raspberry Pi 5 environment used for verification already provides OpenSSL
3.5.x development metadata through `pkg-config`, while libsodium is absent.
The adapter uses the supported EVP Ed25519 interface.

Ed25519 signing and verification use one-shot EVP operations with a NULL digest.

## Build

```bash
make clean
make
make test
```

Dependencies:

```text
C17 compiler
make
pkg-config
OpenSSL development package
```

## Generate an issuer keypair

```bash
./build/qnova approval keygen-ed25519 \
  --private issuer-private.key \
  --public issuer-public.key
```

The private key file is a raw 32-byte Ed25519 seed and is written with mode
`0600` on Unix-like systems. Never commit it.

## Issue a signed approval

```bash
./build/qnova approval issue-ed25519 \
  examples/approval_model.qn \
  model.exec \
  --private-key issuer-private.key \
  --expires-at 2000003600 \
  --context local-review \
  -o model.qns
```

## Verify

```bash
./build/qnova approval verify-ed25519 \
  examples/approval_model.qn \
  model.qns \
  --public-key issuer-public.key
```

## Execute

```bash
./build/qnova run examples/approval_model.qn \
  --signed-approval-file model.qns \
  --approval-public-key-file issuer-public.key \
  --receipt build/model-receipt.json
```

## Canonical token

The signed binary prefix contains:

- `QNAT1` magic
- fixed 32-byte domain separation field
- SHA-256 issuer fingerprint
- stable versioned capability ID
- exact source SHA-256
- issue and expiry times
- 16-byte nonce
- big-endian context length
- context bytes

A 64-byte Ed25519 signature is appended. The parser rejects trailing bytes,
truncation, unknown capability IDs and oversized contexts.

## Stable capability IDs

| ID | Capability | Policy |
|---:|---|---|
| `0x00000001` | `quantum.simulate` | safe |
| `0x00000002` | `evidence.emit` | safe |
| `0x00000100` | `model.exec` | approval required |
| `0x00000101` | `file.write` | approval required |
| `0x00000102` | `network` | approval required |
| `0x00000103` | `device.control` | approval required |
| `0x80000001` | `shell.exec` | blocked |

A valid signature cannot override a blocked capability.

## Honest limitations

- No replay ledger is implemented in v0.5.
- No revocation store is implemented in v0.5.
- The verifier trusts the public-key file explicitly supplied by the operator.
- Raw key files are used; encrypted PKCS#8 support is not implemented.
- This remains a software virtual QCPU, not physical quantum hardware.
- ARM NEON tensor work is deferred to a later performance stage.

## Project separation

QBIT NOVA C remains a separate frozen Devpost project and is not modified.
