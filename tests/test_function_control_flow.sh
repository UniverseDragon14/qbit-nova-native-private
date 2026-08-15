#!/usr/bin/env bash
set -euo pipefail

BIN="${BIN:-./build/qnova}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

expect_check_fail() {
  local label="$1"
  local file="$2"
  set +e
  "$BIN" check "$file" >"$TMP/$label.out" 2>"$TMP/$label.err"
  local status=$?
  set -e
  test "$status" -ne 0
  test ! -s "$TMP/$label.out"
}

expect_exec_fail() {
  local label="$1"
  local file="$2"
  shift 2
  set +e
  "$BIN" exec "$file" "$@" >"$TMP/$label.out" 2>"$TMP/$label.err"
  local status=$?
  set -e
  test "$status" -ne 0
}

echo "=== STAGE7 STEP8 FUNCTION CONTROL FLOW ==="

cat >"$TMP/if-both.qn" <<'QN'
fn min(left: u32, right: u32) -> u32 {
    less = left < right
    if less {
        return left
    } else {
        return right
    }
}
let a: u32 = 10
let b: u32 = 20
call min(a, b) -> answer
emit answer
QN

cat >"$TMP/if-fallthrough.qn" <<'QN'
fn choose(left: u32, right: u32) -> u32 {
    less = left < right
    if less {
        return left
    } else {
        set right = right + left
    }
    return right
}
let a: u32 = 30
let b: u32 = 20
call choose(a, b) -> answer
emit answer
QN

cat >"$TMP/repeat-twice.qn" <<'QN'
fn bump(value: u32, one: u32) -> u32 {
    repeat 3 {
        set value = value + one
    }
    return value
}
let start: u32 = 1
let one: u32 = 1
call bump(start, one) -> first
call bump(start, one) -> second
sum = first + second
emit sum
QN

cat >"$TMP/nested.qn" <<'QN'
fn min(left: u32, right: u32) -> u32 {
    less = left < right
    if less {
        return left
    } else {
        return right
    }
}
fn wrap(left: u32, right: u32) -> u32 {
    call min(left, right) -> value
    return value
}
let a: u32 = 40
let b: u32 = 9
call wrap(a, b) -> answer
emit answer
QN

cat >"$TMP/input.qn" <<'QN'
fn min(left: u32, right: u32) -> u32 {
    less = left < right
    if less {
        return left
    } else {
        return right
    }
}
input price: u32
input qty: u32
call min(price, qty) -> answer
emit answer
QN

cat >"$TMP/repeat-1.qn" <<'QN'
fn bump(value: u32, one: u32) -> u32 {
    repeat 1 {
        set value = value + one
    }
    return value
}
let value: u32 = 10
let one: u32 = 2
call bump(value, one) -> answer
emit answer
QN

cat >"$TMP/repeat-1024.qn" <<'QN'
fn bump(value: u32, one: u32) -> u32 {
    repeat 1024 {
        set value = value + one
    }
    return value
}
let value: u32 = 0
let one: u32 = 1
call bump(value, one) -> answer
emit answer
QN

for name in if-both if-fallthrough repeat-twice nested repeat-1 repeat-1024; do
  "$BIN" check "$TMP/$name.qn" >/dev/null
  "$BIN" build "$TMP/$name.qn" -o "$TMP/$name.qbc" >/dev/null
done
"$BIN" build "$TMP/input.qn" -o "$TMP/input.qbc" >/dev/null

"$BIN" exec "$TMP/if-both.qbc" >"$TMP/if-both.run"
"$BIN" exec "$TMP/if-fallthrough.qbc" >"$TMP/if-fallthrough.run"
"$BIN" exec "$TMP/repeat-twice.qbc" >"$TMP/repeat-twice.run"
"$BIN" exec "$TMP/nested.qbc" >"$TMP/nested.run"
"$BIN" exec "$TMP/repeat-1.qbc" >"$TMP/repeat-1.run"
"$BIN" exec "$TMP/repeat-1024.qbc" >"$TMP/repeat-1024.run"
"$BIN" exec "$TMP/input.qbc" --input qty=20 --input price=10 >"$TMP/input.run"

grep -Fxq 'emitted_u32=10' "$TMP/if-both.run"
grep -Fxq 'emitted_u32=50' "$TMP/if-fallthrough.run"
grep -Fxq 'emitted_u32=8' "$TMP/repeat-twice.run"
grep -Fxq 'emitted_u32=9' "$TMP/nested.run"
grep -Fxq 'emitted_u32=12' "$TMP/repeat-1.run"
grep -Fxq 'emitted_u32=1024' "$TMP/repeat-1024.run"
grep -Fxq 'emitted_u32=10' "$TMP/input.run"
grep -Fxq 'qbc_version=8' "$TMP/if-both.run"
grep -Fxq 'qbc_version=9' "$TMP/input.run"

echo "FUNCTION_COMPARISON_BOOL=PASS"
echo "FUNCTION_IF_BOTH_RETURN=PASS"
echo "FUNCTION_IF_RETURN_FALLTHROUGH=PASS"
echo "FUNCTION_BOUNDED_REPEAT=PASS"
echo "FUNCTION_REPEAT_FRAME_ISOLATION=PASS"
echo "FUNCTION_NESTED_CALL_CONTROL_FLOW=PASS"
echo "RUNTIME_INPUT_FUNCTION_CONTROL_FLOW=PASS"
echo "FUNCTION_REPEAT_BOUNDARY_1=PASS"
echo "FUNCTION_REPEAT_BOUNDARY_1024=PASS"

"$BIN" build "$TMP/if-both.qn" -o "$TMP/deterministic-a.qbc" >/dev/null
"$BIN" build "$TMP/if-both.qn" -o "$TMP/deterministic-b.qbc" >/dev/null
cmp "$TMP/deterministic-a.qbc" "$TMP/deterministic-b.qbc"
echo "STEP8_QBC_DETERMINISTIC=PASS"

set +e
"$BIN" exec "$TMP/if-both.qbc" --backend vulkan >"$TMP/vulkan.out" 2>"$TMP/vulkan.err"
status=$?
set -e
test "$status" -eq 7
test ! -s "$TMP/vulkan.out"
grep -q 'QN-E7201' "$TMP/vulkan.err"
echo "STEP8_GPU_FAIL_CLOSED=PASS"

cat >"$TMP/bad-u32-condition.qn" <<'QN'
fn bad(a: u32, b: u32) -> u32 {
    if a {
        return a
    } else {
        return b
    }
}
let a: u32 = 1
let b: u32 = 2
call bad(a,b) -> out
emit out
QN

cat >"$TMP/bad-bool-return.qn" <<'QN'
fn bad(a: u32, b: u32) -> u32 {
    less = a < b
    return less
}
let a: u32 = 1
let b: u32 = 2
call bad(a,b) -> out
emit out
QN

cat >"$TMP/bad-bool-arg.qn" <<'QN'
fn id(value: u32) -> u32 {
    return value
}
fn bad(a: u32, b: u32) -> u32 {
    less = a < b
    call id(less) -> out
    return out
}
let a: u32 = 1
let b: u32 = 2
call bad(a,b) -> out
emit out
QN

cat >"$TMP/bad-missing-return.qn" <<'QN'
fn bad(a: u32, b: u32) -> u32 {
    less = a < b
    if less {
        set a = a + b
    } else {
        set b = b + a
    }
}
let a: u32 = 1
let b: u32 = 2
call bad(a,b) -> out
emit out
QN

cat >"$TMP/bad-after-return.qn" <<'QN'
fn bad(a: u32) -> u32 {
    return a
    set a = a + a
}
let a: u32 = 1
call bad(a) -> out
emit out
QN

cat >"$TMP/bad-return-in-repeat.qn" <<'QN'
fn bad(a: u32) -> u32 {
    repeat 2 {
        return a
    }
    return a
}
let a: u32 = 1
call bad(a) -> out
emit out
QN

cat >"$TMP/bad-branch-let.qn" <<'QN'
fn bad(a: u32, b: u32) -> u32 {
    less = a < b
    if less {
        let x: u32 = 1
        return a
    } else {
        return b
    }
}
let a: u32 = 1
let b: u32 = 2
call bad(a,b) -> out
emit out
QN

cat >"$TMP/bad-nested-if.qn" <<'QN'
fn bad(a: u32, b: u32) -> u32 {
    less = a < b
    if less {
        if less {
            return a
        } else {
            return b
        }
    } else {
        return b
    }
}
let a: u32 = 1
let b: u32 = 2
call bad(a,b) -> out
emit out
QN

for f in bad-u32-condition bad-bool-return bad-bool-arg bad-missing-return \
         bad-after-return bad-return-in-repeat bad-branch-let bad-nested-if; do
  expect_check_fail "$f" "$TMP/$f.qn"
done

echo "STEP8_SOURCE_NEGATIVE_CONTRACTS=PASS"

python3 - "$TMP/if-both.qbc" "$TMP/tamper-cross-jump.qbc" \
          "$TMP/tamper-bool-return.qbc" <<'PY'
import struct
import sys
from pathlib import Path
src = bytearray(Path(sys.argv[1]).read_bytes())
version = struct.unpack_from('<H', src, 4)[0]
assert version == 8
header = struct.unpack_from('<H', src, 6)[0]
count = struct.unpack_from('<I', src, 8)[0]
fn_count = struct.unpack_from('<H', src, 88)[0]
main = struct.unpack_from('<I', src, 92)[0]
ins_at = header + fn_count * 12

def ins_off(pc): return ins_at + pc * 8

jif = next(pc for pc in range(main) if src[ins_off(pc)] == 0x5e)
struct.pack_into('<I', src, ins_off(jif) + 4, main)
Path(sys.argv[2]).write_bytes(src)

src = bytearray(Path(sys.argv[1]).read_bytes())
comparison = next(pc for pc in range(main) if src[ins_off(pc)] in range(0x57, 0x5d))
bool_slot = src[ins_off(comparison) + 1]
ret = next(pc for pc in range(comparison + 1, main) if src[ins_off(pc)] == 0x67)
src[ins_off(ret) + 1] = bool_slot
Path(sys.argv[3]).write_bytes(src)
PY

expect_exec_fail tamper-cross-jump "$TMP/tamper-cross-jump.qbc"
expect_exec_fail tamper-bool-return "$TMP/tamper-bool-return.qbc"

python3 - "$TMP/repeat-twice.qbc" "$TMP/tamper-repeat-next.qbc" <<'PY'
import struct
import sys
from pathlib import Path
src = bytearray(Path(sys.argv[1]).read_bytes())
header = struct.unpack_from('<H', src, 6)[0]
fn_count = struct.unpack_from('<H', src, 88)[0]
main = struct.unpack_from('<I', src, 92)[0]
ins_at = header + fn_count * 12

def off(pc): return ins_at + pc * 8
next_pc = next(pc for pc in range(main) if src[off(pc)] == 0x65)
old = struct.unpack_from('<I', src, off(next_pc) + 4)[0]
struct.pack_into('<I', src, off(next_pc) + 4, old + 1)
Path(sys.argv[2]).write_bytes(src)
PY
expect_exec_fail tamper-repeat-next "$TMP/tamper-repeat-next.qbc"

echo "STEP8_QBC_TAMPER_REJECTION=PASS"

"$BIN" build examples/u32_function_two.qn -o "$TMP/step6.qbc" >/dev/null
"$BIN" build examples/u32_runtime_input_two.qn -o "$TMP/step7.qbc" >/dev/null

test "$(sha256sum "$TMP/step6.qbc" | awk '{print $1}')" = \
  '159aa91977e174937a2a4a04923d01a79d2fadcacc7a6897b6e32bc3263efb67'
test "$(sha256sum "$TMP/step7.qbc" | awk '{print $1}')" = \
  '2415b4eba99c6f5c79555fea9999b8162fe53eacae07434627758c03655a84f3'

echo "STEP6_FUNCTION_QBC_V8_UNCHANGED=PASS"
echo "STEP7_RUNTIME_INPUT_QBC_V9_UNCHANGED=PASS"
echo "QBC_V10_CREATED=NO"
echo "PASS: QBIT_NOVA_FUNCTION_CONTROL_FLOW_V07_STEP8"
