# Stage8 Step1 GPIO proof — 2026-09-04

## Scope

This record covers portable compiler/runtime and non-mutating mock-backend
evidence for `stage8-step1-isolated-gpio-output`. It does not claim a physical
GPIO run.

```text
base_commit=b4c9e5d21305bdda5ac7c2dc1c8d45a44bb34480
implementation_commit=c88788cc8354139120c9b8c81aadb0168d0f36c7
example_source_sha256=d51b34922cc9df0c4f8c5a40093d1aacca4eff195f07ac98d29185b2edc33d11
qbc_v10_sha256=c99c6e71ab83fc9635bfebf6a3334e89f6fcf1d175dd6b543645002832c58d47
qbc_v10_bytes=136
physical_device=false
```

## Verified results

```text
DEVICE_GPIO_CONTRACT_TESTS=PASS
DEVICE_CONTEXTUAL_SYNTAX=PASS
DEVICE_QBC_V10_REPRODUCIBLE=PASS
DEVICE_APPROVAL_RED_GREEN=PASS
DEVICE_MOCK_EXECUTION=PASS
DEVICE_RUN_EXEC_EQUIVALENCE=PASS
DEVICE_PREFLIGHT_BEFORE_REPLAY=PASS
DEVICE_PHYSICAL_ED25519_ONLY=PASS
DEVICE_NEGATIVE_CONTRACTS=PASS
```

`make test` passed all portable suites. The runner reported supported V3D
hardware unavailable, skipped only the real V3D proof, and passed the bounded
GPU CPU-fallback/result-match path. Existing QBC v8 and v9 compatibility hashes
remained unchanged.

The replay-ledger concurrent first-create race discovered during regression was
repaired. Its concurrency unit suite passed 10 consecutive standalone runs and
the final full test run.

AddressSanitizer and UndefinedBehaviorSanitizer passed the Stage8 contract and
integration tests with leak detection disabled. LeakSanitizer itself was not
available in the ptraced test environment.

## Fail-closed evidence

- Missing `device.control` approval returns `QN-E-APPROVAL-001` before backend
  execution.
- Default `deny` backend returns `QN-E7811` before replay consumption.
- Physical GPIO rejects HMAC approval with `QN-E7820`; Ed25519 is required.
- Invalid source shape, mode mixing, duplicate devices, unknown devices,
  missing emit and out-of-range lines are rejected.
- Nonzero QBC v10 reserved bytes, invalid GPIO values and non-device QBC v10
  payloads are rejected.

## Remaining physical gate

The Linux GPIO-v2 code compiled under strict C17 warnings-as-errors, but no
`/dev/gpiochipN` was opened in this proof environment. A physical claim requires
the Pi operator to confirm the correct gpiochip and line, safe 3.3 V LED wiring,
then run the separately signed command in `docs/DEVICE_GPIO_V08_STEP1.md`.
