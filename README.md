# QBIT NOVA Native — Private Development Branch

Native C17 language/runtime research with a deterministic QBC/QVM pipeline, software quantum-state simulation, bounded native compute, and approval-first execution controls.

## Truth boundary

~~~text
version=0.5.0
runtime=C17
python_dependency=false
boundary=software_virtual_qcpu,native_bounded_compute
physical_qpu=false
~~~

The Raspberry Pi 5 is the classical host. CPU/Vulkan paths are simulation or bounded classical compute; they do not turn the Pi into a physical quantum computer.

All **21 branches** were inspected on 2026-09-01. See [BRANCHES.md](BRANCHES.md) for the complete Stage5 → Stage7 → V10 map, duplicate pointers, public-repository gap, and visibility warning.

## Current command surface

- lex, check, and typed QIR inspection;
- deterministic QBC build and QBC execution;
- source execution with shots, seed, policy, runtime u32 inputs, backend selection, and JSON receipts;
- HMAC compatibility approvals;
- OpenSSL Ed25519 key generation, issuance, and verification;
- trusted issuer, replay-ledger, and revocation-store options;
- capability guard;
- CPU/auto/Vulkan probe and compute-proof operations.

~~~bash
make
make test
./build/qnova version
./build/qnova run examples/ghz3.qn --shots 200000 --seed 20260825 --backend cpu
~~~

## Architecture

~~~text
.qn source
  -> lexer
  -> parser / typed AST
  -> typed QIR
  -> deterministic QBC
  -> QVM
  -> capability guard
  -> authenticated approval / trust checks
  -> bounded backend execution
  -> evidence receipt
~~~

## Current branch scope

The default branch contains later GPU routing, replay/revocation, typed u32 arithmetic/functions, runtime input, and native tensor-memory development beyond the historical stage described by **MANIFEST.json**.

**MANIFEST.json** identifies an older v0.5-stage5.1, 40-file snapshot. It must be regenerated before it can be treated as an attestation of the current branch.

The front end can recognize native tensor programs, but some tensor execution paths remain explicitly incomplete. Read check/run output rather than inferring support from syntax presence.

## Verification boundary

The [Pi5 GHZ3 200,000-shot evidence](docs/proofs/PI5_GHZ3_200K_20260825.md) records deterministic source/QBC histogram agreement: 100,198 × `000`, 99,802 × `111`, zero invalid states. The repository source SHA-256 matches the runtime-reported source hash exactly. This is reproducible software-runtime evidence, not physical-QPU evidence; the raw JSON receipts were not imported into this update.

GPU/Vulkan success is environment-dependent. The test suite distinguishes explicit CPU use, eligible Vulkan paths, and fallback/rejection behavior.

## Security

- Deny unknown or blocked capabilities.
- Keep private signing keys and approval tokens outside Git.
- Use replay and revocation files with owner-only permissions.
- Do not treat a receipt hash as an external signature unless the relevant signed-approval path was used.
- Review conversion warnings and run sanitizers on release builds.

## Status

Active private research branch, not a production security product or physical quantum computer.
