# QBIT NOVA Native — Stage 7 Step 7 (QBC v9)

Native C17 programming-language and deterministic QBC/QVM runtime by Universal Dragon Aslam.

> Current engineering checkpoint: **Stage 7 Step 7 — Runtime Inputs**  
> Current QBC format for runtime-input programs: **QBC v9**  
> Final target: **v10 — not released, not finished**

## Truth boundary

```text
physical_qpu=false
runtime=software_virtual_qcpu
python_runtime_dependency=false
current_stage=stage7_step7_runtime_inputs
current_qbc_runtime_input_version=9
final_v10=false
release_class=private_engineering_checkpoint
```

QBIT NOVA Native is software running on classical hardware. It does not claim that Raspberry Pi 5 or any ordinary computer becomes a physical quantum computer.

## Current pipeline

```text
.qn source
  -> deterministic lexer
  -> parser / AST
  -> semantic validation
  -> typed QIR
  -> QBC
  -> capability guard
  -> approval / trust / revocation / replay checks
  -> native QVM
  -> deterministic result
  -> evidence receipt
```

## Stage history

```text
Stage 5.2   Trust + replay + revocation security freeze
Stage 6     Raspberry Pi 5 / bounded V3D GPU proof freeze
Stage 7.1   Typed u32 scalar
Stage 7.2   u32 arithmetic
Stage 7.3   comparisons + bool
Stage 7.4   if / else control flow
Stage 7.5   bounded repeat
Stage 7.6   native typed functions / QBC v8 freeze
Stage 7.7   deterministic runtime u32 inputs / QBC v9 freeze
v10         FINAL TARGET — not released yet
```

## Stage 7 Step 7 — Runtime Inputs

Runtime inputs are explicit typed language declarations, not blind external overwrites.

```qn
input price: u32
input qty: u32
sum = price + qty
emit sum
```

Execution:

```bash
./build/qnova run program.qn \
  --input price=120 \
  --input qty=3
```

Compiled QBC can also be executed with inputs:

```bash
./build/qnova exec program.qbc \
  --input price=120 \
  --input qty=3
```

The Step 7 runtime-input ABI supports deterministic input-name identities, canonical input slots, u32 values, deterministic input digests, receipt redaction, and QBC v9 validation.

Current limits include:

- maximum runtime inputs: 16
- runtime input ABI: 1
- runtime input type: u32
- QBC v9 header size: 104 bytes
- QBC v9 runtime-input record size: 36 bytes
- runtime-input programs are CPU-routed and explicit Vulkan requests fail closed

## Frozen Step 6 compatibility

Stage 7 Step 7 does **not** reopen or rewrite the frozen Step 6 QBC v8 function checkpoint.

```text
Step6 commit:
3640fb2aa4f61c6da3794903da44bba5b649db15

Frozen Step6 function QBC v8 SHA256:
159aa91977e174937a2a4a04923d01a79d2fadcacc7a6897b6e32bc3263efb67
```

## Current Step 7 freeze checkpoint

```text
Implementation commit:
d17b1f94bf82aef55427a5405193a97b0ccddfe8

Tag:
qbit-nova-native-stage7-step7-runtime-inputs-freeze

Canonical QBC v9 SHA256:
2415b4eba99c6f5c79555fea9999b8162fe53eacae07434627758c03655a84f3

Runtime input receipt SHA256:
da67a87f09f0bb6090d0e2092179edef51cd38f5fa17c72d1aee6e190979df99
```

The current freeze tag is an annotated Git tag but is **unsigned**. The checkpoint remains valid as a repository tag, but future final release provenance should use a signed tag and/or signed final manifest.

## Security layers

- OpenSSL Ed25519 signed approvals
- trusted issuer store
- token and issuer revocation checks
- atomic persistent replay ledger
- deny-by-default capability guard
- bounded execution preflight
- deterministic receipts
- `shell.exec` blocked

HMAC v0.4 remains only a legacy compatibility path.

## Raspberry Pi 5 / GPU boundary

Stage 6 includes a bounded deterministic Vulkan compute proof for Raspberry Pi 5 V3D hardware. It validates a fixed uint32 vector-add workload against a deterministic CPU reference.

This is not arbitrary shader execution and is not a physical QPU claim.

## Build and test

Requirements:

```text
C17 compiler
GNU Make
pkg-config
OpenSSL development package
```

Build:

```bash
make clean
make
make test
```

Strict flags:

```text
-std=c17
-O2
-Wall
-Wextra
-Wpedantic
-Werror
```

The test target includes trust-store, replay, revocation, GPU adapter/compute/routing, typed scalar, arithmetic, comparisons, if/else, bounded repeat, native functions, the main regression suite, and Stage 7 runtime-input tests.

## Release / freeze policy

Stage checkpoints are currently preserved using Git tags.

A future **v10 final release** should generate a fresh final freeze manifest from the exact final source tree and final build artifacts, then sign the release provenance. The old Stage 5.1 manifest must not be treated as the final v10 manifest because it describes an earlier source state.

Recommended final v10 provenance set:

```text
final commit SHA
signed annotated Git tag
fresh source-tree manifest
source archive SHA256
reproducible binary SHA256
final QBC format / ABI hashes
complete regression evidence
signature / signer identity
```

## Status

**QBIT NOVA Native is not finished yet.**

Current engineering state is Stage 7 Step 7 with QBC v9 runtime inputs. **v10 is reserved for the final milestone and must not be claimed until the final implementation, regression, freeze, manifest, and release-signing work is complete.**
