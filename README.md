# QBIT NOVA Native — V10 Data Step 2D Conformance

> Experimental V10 checkpoint; not a released or frozen runtime ABI.

## Branch checkpoint

- Branch: `dev/v10-native-data-step2d-conformance-repair`
- Inspected code tip: `7e5ce96933e1`
- Focus: Repairs canonical frontend and QBC v10 conformance and runs the full regression path.
- Full repository branch inventory: [audited branch map](https://github.com/UniverseDragon14/qbit-nova-native-private/blob/stage7-step9-native-tensor-memory/BRANCHES.md)

## Truth boundary

`boundary=software_virtual_qcpu,native_bounded_compute`  
`physical_qpu=false`

Native C17 language foundation by Universal Dragon Aslam.

## V10 native data + voice development checkpoint

V10 development is happening on isolated development branches while the frozen
Stage7 Step9 checkpoint remains untouched.

Current V10 data path:

```text
.qn source
  -> V10 native-data router
  -> bounded f32 / UTF-8 string / bytes parser
  -> typed native-data AST
  -> Step1 semantic validators
  -> typed QIR + bounded constant pool
  -> canonical QBC v10 data-only encoder
```

Step 2C connects the previously isolated V10 frontend and QBC v10 layout to the
normal `qnova check`, `qnova qir`, and `qnova build` commands. Legacy programs
continue through the existing compiler entrypoint unchanged.

Example:

```qbit
let gain: f32 = 0.75
let greeting: string = "Hi bro 😊"
let packet: bytes = b"QBIT\x00NOVA"
```

Build it with the normal CLI:

```bash
./build/qnova check examples/v10_native_data_qbc10.qn
./build/qnova qir examples/v10_native_data_qbc10.qn
./build/qnova build examples/v10_native_data_qbc10.qn \
  -o build/v10-native-data.qbc
```

The produced file uses the locked QBC v10 data layout: version `10`, a
128-byte canonical header, 80-byte typed value records, and a bounded
constant pool for UTF-8 strings and bytes.

### V10 execution boundary

QBC v10 **data compilation is enabled**, but QVM execution of `f32`, `string`,
and `bytes` is not implemented yet. `qnova run` on V10 native-data source and
`qnova exec` on a QBC v10 data file therefore fail closed with explicit V10
runtime diagnostics instead of falling through to the legacy VM.

The voice/media ABI foundation exists separately for bounded `f32`, UTF-8
strings, bytes, typed audio buffers, and typed voice requests. Audio playback,
TTS synthesis, microphone capture, and Pi5 runtime proof remain later gates.

### Frozen compatibility boundary

- Frozen Stage7 Step9: `f13ccf4e279a792261a5f2cabbd6cd545cc86f0a`
- Legacy QBC v8/v9 compiler path remains available for legacy source.
- QBC v10 is a development format, not a released/frozen runtime ABI yet.
- No release/final freeze is implied by the V10 development branches.

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

## Stage 5 historical limitations

At the Stage 5 checkpoint specifically:

- No replay ledger was implemented yet.
- No revocation store was implemented yet.
- The verifier trusted the public-key file explicitly supplied by the operator.
- Raw key files were used; encrypted PKCS#8 support was not implemented.

Current broader project boundaries still include:

- This remains a software virtual QCPU, not physical quantum hardware.
- V10 native-data QVM execution is not implemented yet.
- V10 audio playback, TTS synthesis, and microphone capture are not implemented yet.

## Project separation

QBIT NOVA C remains a separate frozen Devpost project and is not modified.
