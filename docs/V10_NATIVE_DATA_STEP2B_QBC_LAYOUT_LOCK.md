# QBIT NOVA V10 Native Data Step 2B — QBC v10 Layout Lock

## Base checkpoint

- Parent: `92f696674e4abbf97c91f1286cfceaa5f00d4cfe` (Step 2A)
- Development branch: `dev/v10-native-data-step2b-qbc10-layout`
- Frozen Stage7 Step9 remains `f13ccf4e279a792261a5f2cabbd6cd545cc86f0a`.
- This step does not modify the frozen branch, release tags, Pi5, or the legacy V8/V9 encoder/decoder path.

## Why a separate V10 layout exists

QBC v1-v9 use a fixed common prefix and then fixed-size register/function/input/instruction sections. UTF-8 strings and arbitrary bytes are variable-size values, so serializing them into legacy scalar immediates would be ambiguous and unsafe.

Step 2B keeps the existing QBC common prefix through byte 103, then extends version 10 with an explicit typed-value table and bounded constant pool.

## Canonical QBC v10 data-only header

All integers are little-endian.

| Offset | Size | Field | Step 2B rule |
|---:|---:|---|---|
| 0 | 4 | magic | `QBCN` |
| 4 | 2 | version | `10` |
| 6 | 2 | header_size | `128` |
| 8 | 4 | instruction_count | `0` for Step2B data-only |
| 12 | 2 | total_qubits | `0` |
| 14 | 2 | register_count | `0` |
| 16 | 8 | initial_basis | `0` |
| 24 | 4 | default_shots | `1` |
| 28 | 8 | default_seed | `1` |
| 36 | 32 | source_digest | SHA-256 bytes supplied by caller |
| 68 | 8 | capability_mask | `0` metadata-only |
| 76 | 2 | legacy scalar_count | `0` |
| 78 | 2 | reserved | `0` |
| 80 | 8 | legacy scalar_bool_mask | `0` |
| 88 | 2 | function_count | `0` |
| 90 | 2 | function_record_size | `0` |
| 92 | 4 | main_entry_pc | `0` |
| 96 | 2 | input_count | `0` |
| 98 | 2 | input_record_size | `0` |
| 100 | 2 | input_abi_version | `0` |
| 102 | 2 | reserved | `0` |
| 104 | 2 | value_count | `1..64` |
| 106 | 2 | value_record_size | `80` |
| 108 | 4 | constant_pool_size | `0..16 MiB` |
| 112 | 4 | value_table_offset | `128` |
| 116 | 4 | constant_pool_offset | `128 + value_count*80` |
| 120 | 2 | data_abi_version | `1` |
| 122 | 2 | flags | `DATA_ONLY = 1` |
| 124 | 4 | reserved | `0` |

## Typed value record: 80 bytes

| Offset in record | Size | Field |
|---:|---:|---|
| 0 | 64 | zero-padded UTF-8 identifier |
| 64 | 1 | `QNValueKind` |
| 65 | 1 | flags = `0` |
| 66 | 2 | reserved = `0` |
| 68 | 4 | constant-pool offset |
| 72 | 4 | byte length |
| 76 | 4 | raw f32 bits / zero for blob types |

Canonical rules:

- `f32`: kind `QN_VALUE_F32`, offset `UINT32_MAX`, byte length `4`, finite IEEE-754 bits stored inline.
- `string`: kind `QN_VALUE_STRING`, contiguous constant-pool range, validated bounded UTF-8.
- `bytes`: kind `QN_VALUE_BYTES`, contiguous constant-pool range, arbitrary bounded bytes.
- Blob ranges are declaration-order contiguous. No overlap, gaps, or unreferenced trailing pool bytes.
- Names are unique, NUL-terminated inside 64 bytes, and zero padded after the first NUL.
- Unknown kinds, flags, reserved bits, offsets, malformed UTF-8, size mismatch, and non-finite f32 values fail closed.

## Step 2B implementation

Added isolated APIs:

```c
qn_qbc_v10_data_encode(...)
qn_qbc_v10_data_decode(...)
```

This is a real QBC v10 **data-layout encoder/decoder**, but it is not yet wired into the legacy `qn_compile()` / `qn_qbc_encode()` entrypoints. That separation is deliberate: V8/V9 behavior remains unchanged until the next integration gate.

Example payload used by the Step2B test:

```qbit
let gain: f32 = 0.75
let greeting: string = "Hi bro 😊"
let packet: bytes = b"QBIT\x00NOVA"
```

## Truth boundary

- QBC v10 data layout: IMPLEMENTED in isolated Step2B lane.
- QBC v10 main compiler integration: NOT IMPLEMENTED.
- QVM execution for f32/string/bytes: NOT IMPLEMENTED.
- Full repository regression: NOT RUN here.
- Pi5 proof: NOT RUN.
- Release/final freeze: NOT APPROVED.
