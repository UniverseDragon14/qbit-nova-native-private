#!/usr/bin/env bash
set -euo pipefail

BIN="${BIN:-./build/qnova}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo '=== STAGE8 STEP1 GPIO OUTPUT ==='

"$BIN" check examples/device_gpio_led.qn > "$TMP/check.out"
"$BIN" qir examples/device_gpio_led.qn > "$TMP/qir.out"
grep -Fxq 'QBIT_NOVA_DEVICE_QIR_V08_STEP1' "$TMP/qir.out"
grep -Fq 'GPIO.CONFIG' "$TMP/qir.out"
grep -Fq 'GPIO.WRITE' "$TMP/qir.out"
grep -Fq 'DEVICE.EMIT' "$TMP/qir.out"

"$BIN" build examples/device_gpio_led.qn -o "$TMP/a.qbc" >/dev/null
"$BIN" build examples/device_gpio_led.qn -o "$TMP/b.qbc" >/dev/null
cmp "$TMP/a.qbc" "$TMP/b.qbc"
test "$(od -An -t u1 -j 4 -N 1 "$TMP/a.qbc" | tr -d ' ')" = 10

set +e
"$BIN" run examples/device_gpio_led.qn --device-backend mock \
  > "$TMP/no-approval.out" 2> "$TMP/no-approval.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
test ! -s "$TMP/no-approval.out"
grep -q 'QN-E-APPROVAL-001' "$TMP/no-approval.err"

"$BIN" approval keygen-ed25519 \
  --private "$TMP/device.key" --public "$TMP/device.pub" >/dev/null
"$BIN" approval issue-ed25519 \
  examples/device_gpio_led.qn device.control \
  --private-key "$TMP/device.key" \
  --issued-at 2000000000 --expires-at 2000003600 \
  --nonce-hex 00112233445566778899aabbccddeeff \
  --context stage8-step1-gpio \
  -o "$TMP/device.qns" >/dev/null
printf 'QNRV1\n' > "$TMP/revocation.qnrv"

"$BIN" run examples/device_gpio_led.qn \
  --signed-approval-file "$TMP/device.qns" \
  --approval-public-key-file "$TMP/device.pub" \
  --replay-ledger-file "$TMP/replay.qnrl" \
  --revocation-store-file "$TMP/revocation.qnrv" \
  --now 2000000100 --device-backend mock --device-hold-ms 0 \
  --receipt "$TMP/device.json" > "$TMP/device.out"

grep -Fxq 'QBIT_NOVA_DEVICE_RUN_V08_STEP1' "$TMP/device.out"
grep -Fxq 'approved_capabilities=device.control' "$TMP/device.out"
grep -Fxq 'approval_scheme=ed25519' "$TMP/device.out"
grep -Fxq 'approval_replay=consumed' "$TMP/device.out"
grep -Fxq 'device_backend=mock' "$TMP/device.out"
grep -Fxq 'gpio_line_offset=21' "$TMP/device.out"
grep -Fxq 'write_executed=true' "$TMP/device.out"
grep -Fxq 'safety_reset_low=true' "$TMP/device.out"
grep -Fxq 'mock=true' "$TMP/device.out"
grep -Fq '"qbc_version": 10' "$TMP/device.json"
grep -Fq '"physical_device": false' "$TMP/device.json"

"$BIN" approval issue-ed25519 \
  examples/device_gpio_led.qn device.control \
  --private-key "$TMP/device.key" \
  --issued-at 2000000000 --expires-at 2000003600 \
  --nonce-hex 102132435465768798a9bacbdcedfe0f \
  --context stage8-step1-qbc \
  -o "$TMP/device-exec.qns" >/dev/null

set +e
"$BIN" exec "$TMP/a.qbc" \
  --signed-approval-file "$TMP/device-exec.qns" \
  --approval-public-key-file "$TMP/device.pub" \
  --replay-ledger-file "$TMP/replay-exec.qnrl" \
  --revocation-store-file "$TMP/revocation.qnrv" \
  --now 2000000100 \
  > "$TMP/deny.out" 2> "$TMP/deny.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7811' "$TMP/deny.err"
test ! -e "$TMP/replay-exec.qnrl"

printf '0123456789abcdef0123456789abcdef' > "$TMP/device-hmac.key"
"$BIN" approval issue examples/device_gpio_led.qn device.control \
  --key-file "$TMP/device-hmac.key" \
  --issued-at 2000000000 --expires-at 2000003600 \
  --nonce stage8-hmac -o "$TMP/device-hmac.qna" >/dev/null
set +e
"$BIN" run examples/device_gpio_led.qn \
  --approval-file "$TMP/device-hmac.qna" \
  --approval-key-file "$TMP/device-hmac.key" --now 2000000100 \
  --device-backend linux-gpio --gpiochip /dev/gpiochip0 \
  > "$TMP/hmac-physical.out" 2> "$TMP/hmac-physical.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7820' "$TMP/hmac-physical.err"
test ! -s "$TMP/hmac-physical.out"

"$BIN" exec "$TMP/a.qbc" \
  --signed-approval-file "$TMP/device-exec.qns" \
  --approval-public-key-file "$TMP/device.pub" \
  --replay-ledger-file "$TMP/replay-exec.qnrl" \
  --revocation-store-file "$TMP/revocation.qnrv" \
  --now 2000000100 --device-backend mock --device-hold-ms 0 \
  > "$TMP/exec.out"
grep -Fxq 'QBIT_NOVA_DEVICE_RUN_V08_STEP1' "$TMP/exec.out"
grep -Fxq 'write_executed=true' "$TMP/exec.out"
grep '^qbc_sha256=' "$TMP/device.out" > "$TMP/source-qbc-sha"
grep '^qbc_sha256=' "$TMP/exec.out" > "$TMP/exec-qbc-sha"
cmp "$TMP/source-qbc-sha" "$TMP/exec-qbc-sha"

for CASE in \
  bad_device_missing_requires \
  bad_device_mixed_quantum \
  bad_device_duplicate \
  bad_device_unknown_write \
  bad_device_missing_emit
do
  set +e
  "$BIN" check "tests/${CASE}.qn" \
    > "$TMP/${CASE}.out" 2> "$TMP/${CASE}.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 5
  test ! -s "$TMP/${CASE}.out"
done

set +e
"$BIN" check tests/bad_device_line.qn \
  > "$TMP/bad-line.out" 2> "$TMP/bad-line.err"
STATUS=$?
set -e
test "$STATUS" -eq 4
grep -q 'QN-E7801' "$TMP/bad-line.err"

"$BIN" run tests/device_words_as_identifiers.qn > "$TMP/identifiers.out"
grep -Fxq 'emitted_u32=5' "$TMP/identifiers.out"

echo 'DEVICE_CONTEXTUAL_SYNTAX=PASS'
echo 'DEVICE_QBC_V10_REPRODUCIBLE=PASS'
echo 'DEVICE_APPROVAL_RED_GREEN=PASS'
echo 'DEVICE_MOCK_EXECUTION=PASS'
echo 'DEVICE_RUN_EXEC_EQUIVALENCE=PASS'
echo 'DEVICE_PREFLIGHT_BEFORE_REPLAY=PASS'
echo 'DEVICE_PHYSICAL_ED25519_ONLY=PASS'
echo 'DEVICE_NEGATIVE_CONTRACTS=PASS'
