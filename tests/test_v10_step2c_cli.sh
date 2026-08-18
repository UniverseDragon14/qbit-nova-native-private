#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

V10_SRC="examples/v10_native_data_qbc10.qn"
V10_QBC="$TMP/native-data-v10.qbc"
LEGACY_QBC="$TMP/runtime-input-v9.qbc"

./build/qnova check "$V10_SRC" | tee "$TMP/check.log"
grep -Fxq 'PASS: QBIT_NOVA_NATIVE_DATA_CHECK_V10_STEP2C' "$TMP/check.log"
grep -Fxq 'qbc_version_required=10' "$TMP/check.log"

./build/qnova qir "$V10_SRC" | tee "$TMP/qir.log"
grep -Fxq 'QBIT_NOVA_V10_DATA_QIR_STEP2C' "$TMP/qir.log"
grep -Fxq 'requires_qbc_v10=true' "$TMP/qir.log"
grep -Fq 'value[1].type=string' "$TMP/qir.log"

./build/qnova build "$V10_SRC" -o "$V10_QBC" | tee "$TMP/build-v10.log"
grep -Fxq 'QBIT_NOVA_QBC_V10_DATA_BUILD_STEP2C' "$TMP/build-v10.log"
grep -Fxq 'qbc_version=10' "$TMP/build-v10.log"
grep -Fxq 'qvm_execution=false' "$TMP/build-v10.log"
test -s "$V10_QBC"

set +e
./build/qnova exec "$V10_QBC" >"$TMP/exec-v10.log" 2>&1
EXEC_RC=$?
set -e
test "$EXEC_RC" -eq 7
grep -Fq 'QN-E7832' "$TMP/exec-v10.log"

set +e
./build/qnova run "$V10_SRC" >"$TMP/run-v10.log" 2>&1
RUN_RC=$?
set -e
test "$RUN_RC" -eq 7
grep -Fq 'QN-E7831' "$TMP/run-v10.log"

./build/qnova build examples/u32_runtime_input_two.qn -o "$LEGACY_QBC" \
  | tee "$TMP/build-v9.log"
grep -Fxq 'QBIT_NOVA_QBC_BUILD_V01' "$TMP/build-v9.log"
test -s "$LEGACY_QBC"

echo 'PASS: QBIT_NOVA_V10_NATIVE_DATA_STEP2C_MAIN_INTEGRATION'
