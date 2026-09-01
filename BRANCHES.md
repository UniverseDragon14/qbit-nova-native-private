# QBIT NOVA Native Branch Map

Inspected on **2026-09-01**. All **21 reachable branches** are accounted for. “Inspected code tip” records the implementation tip before this documentation audit; later README-only commits do not change the described code.

| Branch | Inspected code tip | Purpose / state |
|---|---|---|
| `claude/huawei-pura-70-bootloader-hv0xvz` | `a3ccf3ef035e` | Buildroot/QEMU Universal Dragon OS bootstrap; no bootloader unlock |
| `dev/v10-guardian-security-foundation-20260819` | `943c06a01b3c` | V10 guardian security contracts on the V10 data line |
| `dev/v10-native-audio-voice-step1` | `0f7a7ad194e6` | Bounded media/voice ABI foundation; no playback/TTS/mic runtime |
| `dev/v10-native-data-step2b-qbc10-layout` | `21dced5ef75e` | Canonical typed QBC v10 data layout |
| `dev/v10-native-data-step2c-main-integration` | `361930e54d0e` | Normal CLI integration for V10 check/QIR/build |
| `dev/v10-native-data-step2d-conformance-repair` | `7e5ce96933e1` | Canonical frontend/QBC conformance repair |
| `dev/v10-native-data-step2d-regression` | `082a02ccdf92` | Full CI regression enablement for V10 |
| `dev/v10-native-data-step2` | `92f696674e4a` | Gated f32/string/bytes frontend |
| `dragon/huawei-pura-70-bootloader-hv0xvz` | `a3ccf3ef035e` | Duplicate pointer to the same Buildroot/QEMU checkpoint |
| `dragon-voice-v3-smile-poc` | `0b457ac92e39` | Single-u32 smile/emotion packet proof of concept |
| `stage5.2-trust-replay-revocation` | `eaa2bdc3941e` | Trusted issuer, replay ledger, revocation and receipt evidence |
| `stage6-pi5-gpu-adapter` | `8d7c7c663fe5` | CPU/Vulkan routing and bounded V3D classical compute |
| `stage7-native-language-core` | `d64acd4e5641` | Typed u32 scalar language core |
| `stage7-step2-u32-arithmetic` | `d3b0f1d71f3b` | Checked u32 sub/mul/div |
| `stage7-step3-u32-comparisons` | `6b987d879de7` | u32 comparisons and bool |
| `stage7-step4-if-else-control-flow` | `6c8a7f542278` | Deterministic if/else |
| `stage7-step5-bounded-repeat` | `4099e78068f3` | Bounded repeat and explicit mutation |
| `stage7-step6-native-functions` | `3640fb2aa4f6` | Native functions and repaired negative contracts |
| `stage7-step7-runtime-inputs` | `3e4dcf0ff753` | Runtime u32 inputs and QBC v9 |
| `stage7-step8-function-control-flow` | `6749fa8187df` | Deterministic function control flow |
| `stage7-step9-native-tensor-memory` | `f13ccf4e279a` | Frozen Stage7 Step9 tensor-memory checkpoint; default branch |

## Development line

```text
Stage 5 approvals
  -> Stage 5.2 trust/replay/revocation
  -> Stage 6 CPU/Vulkan routing
  -> Stage 7 typed u32 language and control flow
  -> Stage 7 Step9 tensor-memory checkpoint
  -> V10 f32/string/bytes frontend and QBC layout
  -> V10 audio/voice ABI
  -> V10 guardian security contracts
```

The V10 work is split across development branches. It is not a frozen release and its f32/string/bytes QBC execution path remains deliberately fail-closed.

## Duplicate and non-unique branches

The two Huawei-named branches point to the same commit. They contain Buildroot/QEMU OS integration and do **not** contain a demonstrated Huawei bootloader unlock.

Every Git branch points to a commit. Some branches may have no unique commits because two names share one pointer; there is no such thing as a usable Git branch with literally no commit.

## Public repository gap

The separate public repository `UniverseDragon14/qbit-nova-native` currently contains only its older `main` line. Direct checks on 2026-09-01 confirmed these later files are absent there but present in this V10 line:

- `src/gpu_routing.c`
- `examples/u32_runtime_input_one.qn`
- `src/v10_data.c`
- `src/qbc_v10_data.c`
- `src/v10_cli_router.c`
- `include/qn_security_v10.h`
- `examples/v10_native_data_qbc10.qn`

Do not claim that the public repository contains Stage6/Stage7/V10 until an explicit reviewed sync is performed.

## Pi5 evidence

The checked `examples/ghz3.qn` SHA-256 exactly matches the source hash printed by the operator's 200,000-shot Pi5 run. Source execution and QBC execution produced the same `000/111` histogram. See [PI5_GHZ3_200K_20260825.md](docs/proofs/PI5_GHZ3_200K_20260825.md).

This is software virtual-QCPU evidence: `physical_qpu=false`.

## README audit

Nineteen generic/stale branch READMEs were aligned to their actual checkpoint. The already accurate Stage7 Step7 and Stage7 Step9 READMEs were preserved and linked to this audit/evidence where appropriate.

## Manifest warning

The inherited `MANIFEST.json` describes an older Stage5.1 snapshot. It is not an attestation of the later Stage6, Stage7 or V10 branch trees and must be regenerated before a release claim.

## Visibility warning

GitHub visibility is repository-wide, not branch-specific. Making this repository public exposes **all branch histories**, not just the default branch. Complete secret/history review before changing visibility.
