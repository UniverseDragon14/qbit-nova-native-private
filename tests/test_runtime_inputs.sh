#!/usr/bin/env bash
set -euo pipefail

BIN="${BIN:-./build/qnova}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "=== STAGE7 STEP7 RUNTIME INPUTS ==="

"$BIN" build examples/u32_runtime_input_two.qn \
  -o "$TMP/runtime-a.qbc" >/dev/null
"$BIN" build examples/u32_runtime_input_two.qn \
  -o "$TMP/runtime-b.qbc" >/dev/null
cmp "$TMP/runtime-a.qbc" "$TMP/runtime-b.qbc"

RUNTIME_QBC_SHA="$(
  sha256sum "$TMP/runtime-a.qbc" |
  awk '{print $1}'
)"

python3 - "$TMP/runtime-a.qbc" <<'PY'
import struct
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_bytes()

def u16(off):
    return struct.unpack_from("<H", data, off)[0]

def u32(off):
    return struct.unpack_from("<I", data, off)[0]

assert data[:4] == b"QBCN"
assert u16(4) == 9
assert u16(6) == 104
assert u16(88) == 0
assert u16(90) == 12
assert u32(92) == 0
assert u16(96) == 2
assert u16(98) == 36
assert u16(100) == 1
assert u16(102) == 0

input_at = 104
for index in range(2):
    at = input_at + index * 36
    assert len(data[at:at + 32]) == 32
    assert u16(at + 32) == index
    assert data[at + 34] == 1
    assert data[at + 35] == 0

instruction_at = 104 + 2 * 36
assert instruction_at + 3 * 8 == len(data)
assert data[instruction_at] == 0x52
assert data[instruction_at + 8] == 0x53
assert data[instruction_at + 16] == 0x7f

print("QBC_V9_HEADER_104=PASS")
print("QBC_V9_INPUT_RECORD_36=PASS")
print("QBC_V9_INPUT_ABI_1=PASS")
print("QBC_V9_CANONICAL_INPUT_SLOTS=PASS")
PY

echo "RUNTIME_INPUT_QBC_V9_STRUCTURE=PASS"
echo "RUNTIME_INPUT_QBC_V9_SHA256=$RUNTIME_QBC_SHA"
echo "QBC_V9_REPRODUCIBLE=PASS"

echo
echo "=== SOURCE / EXEC EQUIVALENCE ==="

"$BIN" run examples/u32_runtime_input_two.qn \
  --input price=10 \
  --input qty=20 \
  > "$TMP/source.out"

"$BIN" exec "$TMP/runtime-a.qbc" \
  --input price=10 \
  --input qty=20 \
  > "$TMP/exec.out"

grep -Fxq 'emitted_u32=30' "$TMP/source.out"
grep -Fxq 'emitted_u32=30' "$TMP/exec.out"

for FIELD in \
  qbc_version \
  runtime_input_abi \
  runtime_input_count \
  runtime_input_digest \
  emitted_u32 \
  output_sha256
do
  grep "^${FIELD}=" "$TMP/source.out" > "$TMP/source-$FIELD"
  grep "^${FIELD}=" "$TMP/exec.out" > "$TMP/exec-$FIELD"
  cmp "$TMP/source-$FIELD" "$TMP/exec-$FIELD"
done

echo "RUNTIME_INPUT_RUN_EXEC_EQUIVALENCE=PASS"

echo
echo "=== ORDER INDEPENDENCE / RECEIPT ==="

"$BIN" exec "$TMP/runtime-a.qbc" \
  --input price=3141592653 \
  --input qty=2718281828 \
  --receipt "$TMP/order-a.json" \
  > "$TMP/order-a.out"

"$BIN" exec "$TMP/runtime-a.qbc" \
  --input qty=2718281828 \
  --input price=3141592653 \
  --receipt "$TMP/order-b.json" \
  > "$TMP/order-b.out"

grep '^runtime_input_digest=' "$TMP/order-a.out" > "$TMP/digest-a"
grep '^runtime_input_digest=' "$TMP/order-b.out" > "$TMP/digest-b"
cmp "$TMP/digest-a" "$TMP/digest-b"

grep '^emitted_u32=' "$TMP/order-a.out" > "$TMP/value-a"
grep '^emitted_u32=' "$TMP/order-b.out" > "$TMP/value-b"
cmp "$TMP/value-a" "$TMP/value-b"

cmp "$TMP/order-a.json" "$TMP/order-b.json"

grep -Fq '"runtime_input_values_redacted": true' "$TMP/order-a.json"
grep -Fq '"runtime_input_abi": 1' "$TMP/order-a.json"
grep -Fq '"runtime_input_count": 2' "$TMP/order-a.json"
! grep -Fq '3141592653' "$TMP/order-a.json"
! grep -Fq '2718281828' "$TMP/order-a.json"
! grep -Fq -- '--input' "$TMP/order-a.json"

echo "RUNTIME_INPUT_BINDING_ORDER=PASS"
echo "RUNTIME_INPUT_DIGEST_DETERMINISTIC=PASS"
echo "RUNTIME_INPUT_RECEIPT_REPRODUCIBLE=PASS"
echo "RUNTIME_INPUT_RECEIPT_REDACTION=PASS"

echo
echo "=== VALUE BOUNDARIES ==="

"$BIN" run examples/u32_runtime_input_one.qn \
  --input value=0 > "$TMP/zero.out"
grep -Fxq 'emitted_u32=0' "$TMP/zero.out"

"$BIN" run examples/u32_runtime_input_one.qn \
  --input value=4294967295 > "$TMP/max.out"
grep -Fxq 'emitted_u32=4294967295' "$TMP/max.out"

ARGS=()
for i in $(seq 0 15)
do
  ARGS+=(--input "in${i}=$i")
done
"$BIN" run examples/u32_runtime_input_16.qn \
  "${ARGS[@]}" > "$TMP/sixteen.out"
grep -Fxq 'runtime_input_count=16' "$TMP/sixteen.out"
grep -Fxq 'emitted_u32=15' "$TMP/sixteen.out"

echo "RUNTIME_INPUT_ZERO=PASS"
echo "RUNTIME_INPUT_UINT32_MAX=PASS"
echo "MAX_RUNTIME_INPUTS_16=PASS"

echo
echo "=== FUNCTION PASS-BY-VALUE ==="

"$BIN" run examples/u32_runtime_input_function.qn \
  --input left=10 \
  --input right=20 \
  > "$TMP/function.out"
grep -Fxq 'emitted_u32=30' "$TMP/function.out"
grep -Fxq 'parameter_passing=by-value' "$TMP/function.out"

"$BIN" run examples/u32_runtime_input_nested_function.qn \
  --input value=7 \
  > "$TMP/nested.out"
grep -Fxq 'emitted_u32=14' "$TMP/nested.out"

echo "RUNTIME_INPUT_FUNCTION_CALL=PASS"
echo "RUNTIME_INPUT_NESTED_FUNCTION_CALL=PASS"
echo "PARAMETER_PASS_BY_VALUE=PASS"

echo
echo "=== CPU ROUTING ==="

"$BIN" exec "$TMP/runtime-a.qbc" \
  --input price=1 \
  --input qty=2 \
  --backend cpu > "$TMP/cpu.out"
grep -Fxq 'qvm_selected_backend=cpu' "$TMP/cpu.out"
grep -Fxq 'qvm_operation=runtime-input-program' "$TMP/cpu.out"

"$BIN" exec "$TMP/runtime-a.qbc" \
  --input price=1 \
  --input qty=2 \
  --backend auto > "$TMP/auto.out"
grep -Fxq 'qvm_selected_backend=cpu' "$TMP/auto.out"

set +e
"$BIN" exec "$TMP/runtime-a.qbc" \
  --input price=1 \
  --input qty=2 \
  --backend vulkan \
  > "$TMP/vulkan.out" 2> "$TMP/vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
test ! -s "$TMP/vulkan.out"
grep -q 'QN-E7201' "$TMP/vulkan.err"

echo "RUNTIME_INPUT_CPU_ROUTE=PASS"
echo "RUNTIME_INPUT_AUTO_CPU_ROUTE=PASS"
echo "RUNTIME_INPUT_GPU_FAIL_CLOSED=PASS"

echo
echo "=== SOURCE NEGATIVE CONTRACTS ==="

for CASE in \
  bad_input_duplicate_decl \
  bad_input_after_main \
  bad_input_inside_function \
  bad_input_bool
do
  set +e
  "$BIN" check "tests/${CASE}.qn" \
    > "$TMP/${CASE}.out" 2> "$TMP/${CASE}.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 4
  test ! -s "$TMP/${CASE}.out"
done

set +e
"$BIN" check tests/bad_input_overwrite.qn \
  > "$TMP/overwrite.out" 2> "$TMP/overwrite.err"
STATUS=$?
set -e
test "$STATUS" -eq 5
test ! -s "$TMP/overwrite.out"

echo "RUNTIME_INPUT_SOURCE_NEGATIVES=PASS"

echo
echo "=== CLI NEGATIVE CONTRACTS ==="

expect_status() {
  local expected="$1"
  local label="$2"
  shift 2
  set +e
  "$BIN" "$@" > "$TMP/${label}.out" 2> "$TMP/${label}.err"
  local status=$?
  set -e
  test "$status" -eq "$expected"
  test ! -s "$TMP/${label}.out"
}

expect_status 4 missing \
  exec "$TMP/runtime-a.qbc" --input price=1

expect_status 4 duplicate \
  exec "$TMP/runtime-a.qbc" --input price=1 --input price=2

expect_status 4 unknown \
  exec "$TMP/runtime-a.qbc" --input price=1 --input nope=2

expect_status 4 malformed \
  exec "$TMP/runtime-a.qbc" --input price --input qty=2

expect_status 4 empty-value \
  exec "$TMP/runtime-a.qbc" --input price= --input qty=2

expect_status 4 negative \
  exec "$TMP/runtime-a.qbc" --input price=-1 --input qty=2

expect_status 4 overflow \
  exec "$TMP/runtime-a.qbc" --input price=4294967296 --input qty=2

"$BIN" build examples/u32_function_two.qn \
  -o "$TMP/step6-function.qbc" >/dev/null

expect_status 4 old-qbc-input \
  exec "$TMP/step6-function.qbc" --input value=1

echo "RUNTIME_INPUT_CLI_NEGATIVES=PASS"

echo
echo "=== QBC V9 TAMPER REJECTION ==="

tamper_and_reject() {
  local label="$1"
  local mode="$2"
  cp "$TMP/runtime-a.qbc" "$TMP/${label}.qbc"

  python3 - "$TMP/${label}.qbc" "$mode" <<'PY'
import struct
import sys

path, mode = sys.argv[1], sys.argv[2]
with open(path, "r+b") as f:
    data = bytearray(f.read())

if mode == "record-size":
    struct.pack_into("<H", data, 98, 35)
elif mode == "abi":
    struct.pack_into("<H", data, 100, 2)
elif mode == "reserved":
    struct.pack_into("<H", data, 102, 1)
elif mode == "slot":
    struct.pack_into("<H", data, 104 + 36 + 32, 0)
elif mode == "type":
    data[104 + 34] = 2
elif mode == "flags":
    data[104 + 35] = 1
elif mode == "duplicate-digest":
    data[104 + 36:104 + 36 + 32] = data[104:104 + 32]
elif mode == "write-input":
    instruction_at = 104 + 2 * 36
    data[instruction_at + 1] = 0
else:
    raise SystemExit("unknown tamper mode")

with open(path, "wb") as f:
    f.write(data)
PY

  expect_status 6 "$label" \
    exec "$TMP/${label}.qbc" --input price=1 --input qty=2
}

tamper_and_reject bad-record-size record-size
tamper_and_reject bad-abi abi
tamper_and_reject bad-reserved reserved
tamper_and_reject bad-slot slot
tamper_and_reject bad-type type
tamper_and_reject bad-flags flags
tamper_and_reject duplicate-digest duplicate-digest
tamper_and_reject write-input write-input

echo "QBC_V9_RUNTIME_INPUT_TAMPER_REJECTION=PASS"

echo
echo "=== STEP6 QBC FREEZE COMPATIBILITY ==="

STEP6_EXPECTED="159aa91977e174937a2a4a04923d01a79d2fadcacc7a6897b6e32bc3263efb67"
STEP6_ACTUAL="$(
  sha256sum "$TMP/step6-function.qbc" |
  awk '{print $1}'
)"
test "$STEP6_ACTUAL" = "$STEP6_EXPECTED"

echo "STEP6_FUNCTION_QBC_UNCHANGED=PASS"

echo
echo "=== FINAL STEP7 MARKERS ==="
echo "QBC_V9_RUNTIME_INPUT_STRUCTURE=PASS"
echo "RUNTIME_INPUT_BINDING_ABI=PASS"
echo "RUNTIME_INPUT_IMMUTABILITY=PASS"
echo "RUNTIME_INPUT_NEGATIVE_CONTRACTS=PASS"
echo "RUNTIME_INPUT_RECEIPT_REDACTION=PASS"
echo "RUNTIME_INPUTS_PIPELINE=PASS"
echo "PASS: QBIT_NOVA_RUNTIME_INPUTS_V07_STEP7"
