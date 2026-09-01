# QBIT NOVA Native

**A native C17 programming language and deterministic QBC/QVM runtime with a software virtual QCPU and approval-first execution security — built on a Raspberry Pi 5.**

> Full development archive — all stages, all branches, all proofs.
> Created by Universal Dragon Aslam

## Truth boundary

```text
physical_qpu=false
runtime=software_virtual_qcpu
python_runtime_dependency=false
release_class=research_preview
```

QBIT NOVA does not claim that a Raspberry Pi 5 or ordinary software becomes a physical quantum computer. It implements quantum-state simulation and a secure native execution architecture, and every claim in this repository is backed by a hash or a reproducible run.

## How QBIT NOVA works

A `.qn` source file travels one deterministic pipeline. Nothing reaches the machine unless it passes the guard and carries a valid signed approval.

```text
QBIT NOVA source (.qn)
  → deterministic lexer
  → parser
  → typed AST
  → typed QIR
  → QBC bytecode
  → QVM (software state-vector virtual QCPU)
  → capability guard        (deny-by-default)
  → Ed25519 signed approval (OpenSSL EVP)
  → trusted issuer store
  → replay ledger           (atomic, persistent)
  → bounded execution
  → evidence receipt
```

Two execution boundaries are verified to agree: source-level simulation and QBC bytecode execution produce identical results for the same program.

## Verified proofs

**GHZ3 — 200,000 shots, exact agreement (Pi5, 2026-08-25)**

```text
|000> = 100198
|111> =  99802
invalid states = 0
```

Source execution and QBC execution produced the same histogram. The public `examples/ghz3.qn` SHA-256 exactly matches the source used by that run. Full record: [docs/proofs/PI5_GHZ3_200K_20260825.md](docs/proofs/PI5_GHZ3_200K_20260825.md).

**Stage 5.1 frozen checkpoint (MANIFEST.json)**

```text
stage5_1_patch_sha256 : d24ef95798d0dcac2685893951da343bdb9dfa40fdb61463f02c1a36b45cbdcf
frozen_archive_sha256 : cccce237631bed1ce579300ee611fe4986a58dbdbf018e3d93be8edab7bddbf7
source_tree_sha256    : 17355b0cb8b66ddc5fb53172e1dd606f6f15637a1c1ba74300a0171fe5e0efe9
test suite            : QBIT_NOVA_OPENSSL_ED25519_APPROVAL_TEST_SUITE_V05 = PASS
platform              : Raspberry Pi 5, aarch64, OpenSSL 3.5.6
```

Note: `MANIFEST.json` describes the Stage 5.1 snapshot. It is not an attestation of the later Stage 6 / Stage 7 / V10 branch trees and will be regenerated before a release claim.

**Approval security — attack tests**

Valid Ed25519 approvals pass. Replay, wrong-key, expired, tampered, and blocked-shell cases are correctly rejected. `shell.exec` is blocked by default.

## Development timeline

Every stage is a real Git branch in this repository. Nothing is retro-written.

```text
Stage 5    — Ed25519 authenticated approvals
Stage 5.2  — trusted issuer store, replay ledger, revocation
Stage 6    — CPU/Vulkan routing, bounded V3D classical compute (Pi5 GPU adapter)
Stage 7    — typed u32 native language core
  Step 2   — checked u32 arithmetic
  Step 3   — u32 comparisons and bool
  Step 4   — deterministic if/else
  Step 5   — bounded repeat and explicit mutation
  Step 6   — native functions
  Step 7   — runtime u32 inputs, QBC v9
  Step 8   — deterministic function control flow
  Step 9   — native tensor memory (frozen checkpoint, default branch)
V10        — f32/string/bytes frontend, canonical QBC v10 layout, CLI integration,
             conformance repair, CI regression, audio/voice ABI, guardian security
             contracts (split across dev branches; f32/string/bytes QBC path is
             deliberately fail-closed until frozen)
```

The complete branch-by-branch map with inspected commit tips is in [BRANCHES.md](BRANCHES.md).

## Repository layout

```text
src/        native C17 implementation (lexer, parser, QIR, QBC, VM, guard,
            approval, Ed25519, signed approval, replay)
include/    public headers and contracts
examples/   .qn programs (ghz3, bell, single, approval_model, blocked_shell)
tests/      test runner and negative cases
docs/       per-stage design contracts and proofs
```

## Security layers

| Layer | Status |
|---|---|
| Deny-by-default capability guard | Implemented |
| Ed25519 approvals (OpenSSL EVP) | Implemented |
| HMAC-SHA-256 v0.4 compatibility | Implemented |
| Source-scope binding, expiry, tamper detection | Implemented |
| Trusted issuer store | Implemented |
| Atomic persistent replay ledger | Implemented |
| Token / issuer revocation store | Implemented |
| `shell.exec` | Blocked by default |

## Build and test

Requirements: C17 compiler, GNU Make, OpenSSL development package, `pkg-config`.

```bash
make
make test
```

Strict flags: `-std=c17 -O2 -Wall -Wextra -Wpedantic -Werror`

## Publication safety

The [publication security review](docs/PUBLICATION_SECURITY_REVIEW_20260901.md) accounted for 21 branches and 238 reachable/baseline text paths with no recognized secret signatures. Repository visibility remains **HOLD** until deleted historical blobs and binary/archive history receive a complete mirror-based scan.

## Project status

Active research preview. Not yet a production security product, not a physical quantum computer, not the final QBIT NOVA release.

## License

No open-source license has been assigned yet. Visibility does not grant redistribution, modification, or commercial-use rights.
