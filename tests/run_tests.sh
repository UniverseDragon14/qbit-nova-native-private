#!/usr/bin/env bash
set -euo pipefail

BIN="./build/qnova"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

printf '0123456789abcdef0123456789abcdef' > "$TMP/key.bin"
printf 'fedcba9876543210fedcba9876543210' > "$TMP/wrong-key.bin"

echo "=== VERSION ==="
"$BIN" version > "$TMP/version.out"
grep -q 'QBIT NOVA Native 0.4.0' "$TMP/version.out"
grep -q 'python_dependency=false' "$TMP/version.out"

echo "=== REQUIRES DECLARATION ==="
"$BIN" qir examples/approval_model.qn > "$TMP/approval.qir"
grep -q '^capabilities=quantum.simulate,evidence.emit,model.exec$' \
  "$TMP/approval.qir"

echo "=== APPROVAL REQUIRED WITHOUT TOKEN ==="
set +e
"$BIN" run examples/approval_model.qn \
  >"$TMP/no-token.out" 2>"$TMP/no-token.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-001' "$TMP/no-token.err"

echo "=== ISSUE AUTHENTICATED APPROVAL ==="
"$BIN" approval issue \
  examples/approval_model.qn \
  model.exec \
  --key-file "$TMP/key.bin" \
  --issued-at 2000000000 \
  --expires-at 2000003600 \
  --nonce stage4proof \
  -o "$TMP/model.qna" \
  > "$TMP/issue.out"
grep -q '^authentication=hmac-sha256$' "$TMP/issue.out"

echo "=== VERIFY AUTHENTICATED APPROVAL ==="
"$BIN" approval verify \
  examples/approval_model.qn \
  "$TMP/model.qna" \
  --key-file "$TMP/key.bin" \
  --now 2000000100 \
  > "$TMP/verify.out"
grep -q '^status=valid$' "$TMP/verify.out"
grep -q '^capability=model.exec$' "$TMP/verify.out"

echo "=== GUARDED EXECUTION WITH TOKEN ==="
"$BIN" run examples/approval_model.qn \
  --approval-file "$TMP/model.qna" \
  --approval-key-file "$TMP/key.bin" \
  --now 2000000100 \
  --receipt "$TMP/model-receipt.json" \
  > "$TMP/run.out"
grep -q '^guard=allowed$' "$TMP/run.out"
grep -q '^approved_capabilities=model.exec$' "$TMP/run.out"
grep -Eq '^approval_token_sha256=[0-9a-f]{64}$' "$TMP/run.out"
grep -q '"approved_capabilities": "model.exec"' \
  "$TMP/model-receipt.json"

echo "=== WRONG KEY REJECTION ==="
set +e
"$BIN" approval verify \
  examples/approval_model.qn \
  "$TMP/model.qna" \
  --key-file "$TMP/wrong-key.bin" \
  --now 2000000100 \
  >"$TMP/wrong-key.out" 2>"$TMP/wrong-key.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-004' "$TMP/wrong-key.err"

echo "=== TAMPER DETECTION ==="
cp "$TMP/model.qna" "$TMP/tampered.qna"
sed -i 's/nonce=stage4proof/nonce=stage4tamper/' \
  "$TMP/tampered.qna"
set +e
"$BIN" approval verify \
  examples/approval_model.qn \
  "$TMP/tampered.qna" \
  --key-file "$TMP/key.bin" \
  --now 2000000100 \
  >"$TMP/tamper.out" 2>"$TMP/tamper.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-004' "$TMP/tamper.err"

echo "=== SOURCE SCOPE REJECTION ==="
set +e
"$BIN" approval verify \
  examples/ghz3.qn \
  "$TMP/model.qna" \
  --key-file "$TMP/key.bin" \
  --now 2000000100 \
  >"$TMP/scope.out" 2>"$TMP/scope.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-003' "$TMP/scope.err"

echo "=== EXPIRY REJECTION ==="
set +e
"$BIN" approval verify \
  examples/approval_model.qn \
  "$TMP/model.qna" \
  --key-file "$TMP/key.bin" \
  --now 2000003601 \
  >"$TMP/expired.out" 2>"$TMP/expired.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-005' "$TMP/expired.err"

echo "=== BLOCKED CAPABILITY CANNOT BE APPROVED ==="
set +e
"$BIN" approval issue \
  examples/blocked_shell.qn \
  shell.exec \
  --key-file "$TMP/key.bin" \
  --issued-at 2000000000 \
  --expires-at 2000003600 \
  -o "$TMP/shell.qna" \
  >"$TMP/shell.out" 2>"$TMP/shell.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-006' "$TMP/shell.err"

echo "=== SAFE PROGRAM STILL RUNS WITHOUT TOKEN ==="
"$BIN" run examples/ghz3.qn > "$TMP/ghz.out"
grep -q '^QBIT_NOVA_NATIVE_RUN_V04$' "$TMP/ghz.out"
grep -q '^guard=allowed$' "$TMP/ghz.out"
grep -q '^approved_capabilities=none$' "$TMP/ghz.out"

echo "=== DETERMINISTIC QBC ==="
"$BIN" build examples/approval_model.qn \
  -o "$TMP/a.qbc" >/dev/null
"$BIN" build examples/approval_model.qn \
  -o "$TMP/b.qbc" >/dev/null
cmp "$TMP/a.qbc" "$TMP/b.qbc"

echo "=== BELL / GHZ CORRELATION ==="
"$BIN" run examples/bell.qn > "$TMP/bell.out"
if grep -Eq '^\|01>=|^\|10>=' "$TMP/bell.out"; then
  echo "FAIL: invalid Bell state"
  exit 1
fi
if grep -Eq '^\|001>=|^\|010>=|^\|011>=|^\|100>=|^\|101>=|^\|110>=' \
  "$TMP/ghz.out"; then
  echo "FAIL: invalid GHZ state"
  exit 1
fi

echo "PASS: QBIT_NOVA_AUTHENTICATED_APPROVAL_TEST_SUITE_V04"
