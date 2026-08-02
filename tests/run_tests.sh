#!/usr/bin/env bash
set -euo pipefail

# Secure permissions for test revocation files.
umask 077

BIN="./build/qnova"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

printf '0123456789abcdef0123456789abcdef' > "$TMP/hmac.key"
printf '0123456789abcdef0123456789abcdef' > "$TMP/ed-private-a.key"
printf 'fedcba9876543210fedcba9876543210' > "$TMP/ed-private-b.key"

"$BIN" approval derive-public-ed25519 \
  --private "$TMP/ed-private-a.key" \
  --public "$TMP/ed-public-a.key" >/dev/null

"$BIN" approval derive-public-ed25519 \
  --private "$TMP/ed-private-b.key" \
  --public "$TMP/ed-public-b.key" >/dev/null

PUBLIC_A_HEX="$(
  od -An -tx1 -v "$TMP/ed-public-a.key" |
    tr -d '[:space:]'
)"
PUBLIC_B_HEX="$(
  od -An -tx1 -v "$TMP/ed-public-b.key" |
    tr -d '[:space:]'
)"

test "${#PUBLIC_A_HEX}" -eq 64
test "${#PUBLIC_B_HEX}" -eq 64

printf 'QNTS1\nissuer\t%s\tcreator-primary\n' \
  "$PUBLIC_A_HEX" > "$TMP/trust-a.qnts"

printf 'QNTS1\nissuer\t%s\tcreator-secondary\n' \
  "$PUBLIC_B_HEX" > "$TMP/trust-b.qnts"

printf 'QNTS1\n' > "$TMP/trust-empty.qnts"
printf 'BAD!!\n' > "$TMP/trust-malformed.qnts"
printf 'QNRV1\n' > "$TMP/revocation-empty.qnrv"
printf 'BAD!!\n' > "$TMP/revocation-malformed.qnrv"

echo "=== VERSION ==="
"$BIN" version > "$TMP/version.out"
grep -q 'QBIT NOVA Native 0.5.0' "$TMP/version.out"
grep -q 'python_dependency=false' "$TMP/version.out"

echo "=== GPU ADAPTER CONTRACT ==="
"$BIN" gpu probe \
  --backend cpu \
  --receipt "$TMP/gpu-cpu.json" \
  > "$TMP/gpu-cpu.out"
grep -q '^QBIT_NOVA_GPU_ADAPTER_CONTRACT_V06$' \
  "$TMP/gpu-cpu.out"
grep -q '^requested_backend=cpu$' "$TMP/gpu-cpu.out"
grep -q '^selected_backend=cpu$' "$TMP/gpu-cpu.out"
grep -q '^selection_reason=explicit-cpu$' "$TMP/gpu-cpu.out"
grep -q '^gpu_execution_enabled=false$' "$TMP/gpu-cpu.out"
grep -q '^cpu_fallback=true$' "$TMP/gpu-cpu.out"
grep -q '^llvmpipe_hardware_gpu=false$' "$TMP/gpu-cpu.out"
grep -q '"selected_backend": "cpu"' "$TMP/gpu-cpu.json"
grep -q '"gpu_execution_enabled": false' "$TMP/gpu-cpu.json"

"$BIN" gpu probe --backend auto > "$TMP/gpu-auto.out"
grep -q '^requested_backend=auto$' "$TMP/gpu-auto.out"
grep -q '^selected_backend=cpu$' "$TMP/gpu-auto.out"
grep -Eq '^selection_reason=(gpu-kernel-not-implemented|hardware-unavailable)$' \
  "$TMP/gpu-auto.out"
grep -q '^gpu_execution_enabled=false$' "$TMP/gpu-auto.out"
grep -q '^cpu_fallback=true$' "$TMP/gpu-auto.out"

set +e
"$BIN" gpu probe --backend vulkan \
  >"$TMP/gpu-vulkan.out" \
  2>"$TMP/gpu-vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7005' "$TMP/gpu-vulkan.err"
test ! -s "$TMP/gpu-vulkan.out"

echo "=== GPU COMPUTE CPU REFERENCE ==="
"$BIN" gpu compute-proof \
  --backend cpu \
  --receipt "$TMP/gpu-compute-cpu.json" \
  > "$TMP/gpu-compute-cpu.out"
grep -q '^QBIT_NOVA_GPU_COMPUTE_PROOF_V06$' \
  "$TMP/gpu-compute-cpu.out"
grep -q '^selected_backend=cpu$' \
  "$TMP/gpu-compute-cpu.out"
grep -q '^gpu_execution_attempted=false$' \
  "$TMP/gpu-compute-cpu.out"
grep -q '^gpu_execution_completed=false$' \
  "$TMP/gpu-compute-cpu.out"
grep -q '^cpu_reference_validated=true$' \
  "$TMP/gpu-compute-cpu.out"
grep -q '^result_match=true$' \
  "$TMP/gpu-compute-cpu.out"
grep -Eq '^shader_sha256=[0-9a-f]{64}$' \
  "$TMP/gpu-compute-cpu.out"
grep -Eq '^output_sha256=[0-9a-f]{64}$' \
  "$TMP/gpu-compute-cpu.out"
grep -q '"selected_backend": "cpu"' \
  "$TMP/gpu-compute-cpu.json"

echo "=== REAL V3D VULKAN COMPUTE PROOF ==="
"$BIN" gpu compute-proof \
  --backend vulkan \
  --receipt "$TMP/gpu-compute-v3d.json" \
  > "$TMP/gpu-compute-v3d.out"
grep -q '^selected_backend=vulkan$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^selection_reason=explicit-verified-v3d$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^hardware_device=V3D ' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^hardware_vendor_id=0x14e4$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^element_count=256$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^local_size_x=64$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^dispatch_x=4$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^gpu_execution_attempted=true$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^gpu_execution_completed=true$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^cpu_reference_validated=true$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^result_match=true$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '^cpu_fallback=false$' \
  "$TMP/gpu-compute-v3d.out"
grep -q '"selected_backend": "vulkan"' \
  "$TMP/gpu-compute-v3d.json"
grep -q '"gpu_execution_completed": true' \
  "$TMP/gpu-compute-v3d.json"
grep -q '"result_match": true' \
  "$TMP/gpu-compute-v3d.json"

echo "=== OPENSSL ED25519 CSPRNG KEYGEN ==="
"$BIN" approval keygen-ed25519 \
  --private "$TMP/random-a.key" \
  --public "$TMP/random-a.pub" > "$TMP/random-a.out"
"$BIN" approval keygen-ed25519 \
  --private "$TMP/random-b.key" \
  --public "$TMP/random-b.pub" > "$TMP/random-b.out"
if cmp -s "$TMP/random-a.key" "$TMP/random-b.key"; then
  echo "FAIL: generated private keys are identical"
  exit 1
fi
test "$(stat -c '%a' "$TMP/random-a.key")" = "600"

echo "=== HMAC V0.4 COMPATIBILITY ==="
"$BIN" approval issue \
  examples/approval_model.qn \
  model.exec \
  --key-file "$TMP/hmac.key" \
  --issued-at 2000000000 \
  --expires-at 2000003600 \
  --nonce compatibility \
  -o "$TMP/model.qna" >/dev/null
"$BIN" run examples/approval_model.qn \
  --approval-file "$TMP/model.qna" \
  --approval-key-file "$TMP/hmac.key" \
  --now 2000000100 \
  > "$TMP/hmac-run.out"
grep -q '^approval_scheme=hmac-sha256$' "$TMP/hmac-run.out"
grep -q '^approval_revocation=not-applicable$'   "$TMP/hmac-run.out"
grep -q '^approval_token_revoked=not-applicable$'   "$TMP/hmac-run.out"
grep -q '^approval_issuer_revoked=not-applicable$'   "$TMP/hmac-run.out"
grep -q '^approval_replay=not-applicable$'   "$TMP/hmac-run.out"

echo "=== HMAC BLOCKED CAPABILITY REJECTION ==="
set +e
"$BIN" approval issue \
  examples/blocked_shell.qn \
  shell.exec \
  --key-file "$TMP/hmac.key" \
  --issued-at 2000000000 \
  --expires-at 2000003600 \
  --nonce blocked-hmac \
  -o "$TMP/shell.qna" \
  >"$TMP/hmac-shell.out" 2>"$TMP/hmac-shell.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E-APPROVAL-006' "$TMP/hmac-shell.err"

echo "=== ISSUE ED25519 APPROVAL ==="
"$BIN" approval issue-ed25519 \
  examples/approval_model.qn \
  model.exec \
  --private-key "$TMP/ed-private-a.key" \
  --issued-at 2000000000 \
  --expires-at 2000003600 \
  --nonce-hex 00112233445566778899aabbccddeeff \
  --context stage5-proof \
  -o "$TMP/model.qns" \
  > "$TMP/issue.out"
grep -q '^authentication=ed25519$' "$TMP/issue.out"
grep -q '^capability_id=0x00000100$' "$TMP/issue.out"

echo "=== VERIFY ED25519 APPROVAL ==="
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000000100 \
  > "$TMP/verify.out"
grep -q '^status=valid$' "$TMP/verify.out"
grep -q '^key_source=public-key$' "$TMP/verify.out"
grep -q '^capability=model.exec$' "$TMP/verify.out"

echo "=== VERIFY ED25519 THROUGH TRUST STORE ==="
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --now 2000000100 \
  > "$TMP/verify-trust.out"
grep -q '^status=valid$' "$TMP/verify-trust.out"
grep -q '^key_source=trust-store$' "$TMP/verify-trust.out"
grep -q '^capability=model.exec$' "$TMP/verify-trust.out"

TOKEN_SHA="$(
  sed -n 's/^token_sha256=//p' "$TMP/verify.out"
)"
ISSUER_FINGERPRINT="$(
  sed -n 's/^issuer_fingerprint=//p' "$TMP/verify.out"
)"

test "${#TOKEN_SHA}" -eq 64
test "${#ISSUER_FINGERPRINT}" -eq 64

printf 'QNRV1\ntoken\t%s\trevoked-test-token\n' \
  "$TOKEN_SHA" > "$TMP/revocation-token.qnrv"

printf 'QNRV1\nissuer\t%s\tretired-test-issuer\n' \
  "$ISSUER_FINGERPRINT" > "$TMP/revocation-issuer.qnrv"

echo "=== VERIFY COMMAND REMAINS NON-MUTATING ==="
REVOCATION_BEFORE="$(
  sha256sum "$TMP/revocation-empty.qnrv" |
    awk '{print $1}'
)"

"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --now 2000000100 \
  > "$TMP/verify-nonmutating.out"

REVOCATION_AFTER="$(
  sha256sum "$TMP/revocation-empty.qnrv" |
    awk '{print $1}'
)"

test "$REVOCATION_BEFORE" = "$REVOCATION_AFTER"
test ! -e "$TMP/verify-replay.qnrl"

echo "=== MISSING REVOCATION OPTION REJECTION ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-no-revocation-option.qnrl" \
  --now 2000000100 \
  >"$TMP/no-revocation-option.out" \
  2>"$TMP/no-revocation-option.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E6109' "$TMP/no-revocation-option.err"
test ! -e "$TMP/replay-no-revocation-option.qnrl"

echo "=== MISSING REVOCATION FILE FAILS CLOSED ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-missing-revocation.qnrl" \
  --revocation-store-file "$TMP/does-not-exist.qnrv" \
  --now 2000000100 \
  >"$TMP/missing-revocation.out" \
  2>"$TMP/missing-revocation.err"
STATUS=$?
set -e
test "$STATUS" -eq 2
grep -q 'QN-E6106' "$TMP/missing-revocation.err"
test ! -e "$TMP/replay-missing-revocation.qnrl"

echo "=== MALFORMED REVOCATION FILE FAILS CLOSED ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-malformed-revocation.qnrl" \
  --revocation-store-file "$TMP/revocation-malformed.qnrv" \
  --now 2000000100 \
  >"$TMP/malformed-revocation.out" \
  2>"$TMP/malformed-revocation.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E6103' "$TMP/malformed-revocation.err"
test ! -e "$TMP/replay-malformed-revocation.qnrl"

echo "=== REVOKED TOKEN REJECTED BEFORE REPLAY ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-revoked-token.qnrl" \
  --revocation-store-file "$TMP/revocation-token.qnrv" \
  --now 2000000100 \
  >"$TMP/revoked-token.out" \
  2>"$TMP/revoked-token.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E6107' "$TMP/revoked-token.err"
test ! -e "$TMP/replay-revoked-token.qnrl"

echo "=== REVOKED ISSUER REJECTED BEFORE REPLAY ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-revoked-issuer.qnrl" \
  --revocation-store-file "$TMP/revocation-issuer.qnrv" \
  --now 2000000100 \
  >"$TMP/revoked-issuer.out" \
  2>"$TMP/revoked-issuer.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E6108' "$TMP/revoked-issuer.err"
test ! -e "$TMP/replay-revoked-issuer.qnrl"

echo "=== GUARDED EXECUTION WITH ED25519 ==="
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --approval-public-key-file "$TMP/ed-public-a.key" \
  --replay-ledger-file "$TMP/replay-a.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  --receipt "$TMP/receipt-a.json" \
  > "$TMP/signed-run.out"
grep -q '^QBIT_NOVA_NATIVE_RUN_V05$' "$TMP/signed-run.out"
grep -q '^approval_scheme=ed25519$' "$TMP/signed-run.out"
grep -q '^approved_capabilities=model.exec$' "$TMP/signed-run.out"
grep -q '^approval_revocation=checked-clear$'   "$TMP/signed-run.out"
grep -q '^approval_token_revoked=false$'   "$TMP/signed-run.out"
grep -q '^approval_issuer_revoked=false$'   "$TMP/signed-run.out"
grep -q '^approval_replay=consumed$' "$TMP/signed-run.out"
grep -Eq '^approval_issuer_fingerprint=[0-9a-f]{64}$'   "$TMP/signed-run.out"
grep -q '"approval_scheme": "ed25519"' "$TMP/receipt-a.json"
grep -q '"approval_revocation": "checked-clear"'   "$TMP/receipt-a.json"
grep -q '"approval_token_revoked": false'   "$TMP/receipt-a.json"
grep -q '"approval_issuer_revoked": false'   "$TMP/receipt-a.json"
grep -q '"approval_replay": "consumed"'   "$TMP/receipt-a.json"

echo "=== REPLAYED RUN REJECTION ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --approval-public-key-file "$TMP/ed-public-a.key" \
  --replay-ledger-file "$TMP/replay-a.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  >"$TMP/replay-run.out" 2>"$TMP/replay-run.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5204' "$TMP/replay-run.err"

echo "=== GUARDED EXECUTION THROUGH TRUST STORE ==="
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-trust.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  > "$TMP/signed-trust-run.out"
grep -q '^QBIT_NOVA_NATIVE_RUN_V05$' \
  "$TMP/signed-trust-run.out"
grep -q '^approval_scheme=ed25519$' \
  "$TMP/signed-trust-run.out"
grep -q '^approved_capabilities=model.exec$' \
  "$TMP/signed-trust-run.out"
grep -q '^approval_revocation=checked-clear$' \
  "$TMP/signed-trust-run.out"
grep -q '^approval_token_revoked=false$' \
  "$TMP/signed-trust-run.out"
grep -q '^approval_issuer_revoked=false$' \
  "$TMP/signed-trust-run.out"
grep -q '^approval_replay=consumed$' \
  "$TMP/signed-trust-run.out"

echo "=== DETERMINISTIC RECEIPT ==="
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --approval-public-key-file "$TMP/ed-public-a.key" \
  --replay-ledger-file "$TMP/replay-b.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  --receipt "$TMP/receipt-b.json" >/dev/null
cmp "$TMP/receipt-a.json" "$TMP/receipt-b.json"

echo "=== EXEC REPLAY BOUNDARY ==="
"$BIN" build examples/approval_model.qn \
  -o "$TMP/approval-model.qbc" >/dev/null

"$BIN" exec "$TMP/approval-model.qbc" \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-exec.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  > "$TMP/exec-first.out"

grep -q '^approval_revocation=checked-clear$' \
  "$TMP/exec-first.out"
grep -q '^approval_token_revoked=false$' \
  "$TMP/exec-first.out"
grep -q '^approval_issuer_revoked=false$' \
  "$TMP/exec-first.out"
grep -q '^approval_replay=consumed$' \
  "$TMP/exec-first.out"

set +e
"$BIN" exec "$TMP/approval-model.qbc" \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-exec.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  >"$TMP/exec-replay.out" \
  2>"$TMP/exec-replay.err"
STATUS=$?
set -e

test "$STATUS" -eq 7
grep -q 'QN-E5204' "$TMP/exec-replay.err"

echo "=== PREFLIGHT FAILURE DOES NOT CONSUME ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-preflight.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --shots 1000001 \
  --now 2000000100 \
  >"$TMP/preflight.out" 2>"$TMP/preflight.err"
STATUS=$?
set -e
test "$STATUS" -eq 8
grep -q 'QN-E5302' "$TMP/preflight.err"
test ! -e "$TMP/replay-preflight.qnrl"

"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-preflight.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 >/dev/null

echo "=== SIGNED EXECUTION WITHOUT LEDGER REJECTION ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  >"$TMP/no-ledger.out" 2>"$TMP/no-ledger.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5201' "$TMP/no-ledger.err"

echo "=== WRONG PUBLIC KEY REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --public-key "$TMP/ed-public-b.key" \
  --now 2000000100 \
  >"$TMP/wrong-key.out" 2>"$TMP/wrong-key.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5003' "$TMP/wrong-key.err"

echo "=== UNTRUSTED ISSUER STORE REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-b.qnts" \
  --now 2000000100 \
  >"$TMP/untrusted-store.out" \
  2>"$TMP/untrusted-store.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5104' "$TMP/untrusted-store.err"

echo "=== EMPTY TRUST STORE REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-empty.qnts" \
  --now 2000000100 \
  >"$TMP/empty-store.out" \
  2>"$TMP/empty-store.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5104' "$TMP/empty-store.err"

echo "=== MALFORMED TRUST STORE REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-malformed.qnts" \
  --now 2000000100 \
  >"$TMP/malformed-store.out" \
  2>"$TMP/malformed-store.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5113' "$TMP/malformed-store.err"

echo "=== DUAL SIGNED KEY SOURCE REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --now 2000000100 \
  >"$TMP/dual-source.out" \
  2>"$TMP/dual-source.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/dual-source.err"

echo "=== SIGNED TOKEN WITHOUT KEY SOURCE REJECTION ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --replay-ledger-file "$TMP/replay-no-key-source.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  >"$TMP/no-key-source.out" \
  2>"$TMP/no-key-source.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/no-key-source.err"

echo "=== TRUST STORE WITHOUT SIGNED TOKEN REJECTION ==="
set +e
"$BIN" run examples/approval_model.qn \
  --trust-store-file "$TMP/trust-a.qnts" \
  --now 2000000100 \
  >"$TMP/store-without-token.out" \
  2>"$TMP/store-without-token.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/store-without-token.err"

echo "=== REVOCATION STORE WITHOUT SIGNED TOKEN REJECTION ==="
set +e
"$BIN" run examples/approval_model.qn \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --now 2000000100 \
  >"$TMP/revocation-without-token.out" \
  2>"$TMP/revocation-without-token.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E6109' "$TMP/revocation-without-token.err"

echo "=== TAMPERED PAYLOAD REJECTION ==="
cp "$TMP/model.qns" "$TMP/tampered.qns"
printf '\x58' | dd of="$TMP/tampered.qns" \
  bs=1 seek=139 count=1 conv=notrunc status=none
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/tampered.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000000100 \
  >"$TMP/tamper.out" 2>"$TMP/tamper.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5002' "$TMP/tamper.err"

echo "=== TRAILING BYTE REJECTION ==="
cp "$TMP/model.qns" "$TMP/trailing.qns"
printf 'x' >> "$TMP/trailing.qns"
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/trailing.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000000100 \
  >"$TMP/trailing.out" 2>"$TMP/trailing.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/trailing.err"

echo "=== TRUNCATED SIGNATURE REJECTION ==="
cp "$TMP/model.qns" "$TMP/truncated.qns"
truncate -s -1 "$TMP/truncated.qns"
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/truncated.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000000100 \
  >"$TMP/truncated.out" 2>"$TMP/truncated.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/truncated.err"

echo "=== MALFORMED CAPABILITY ID REJECTION ==="
cp "$TMP/model.qns" "$TMP/bad-capability.qns"
printf '\xde\xad\xbe\xef' | dd of="$TMP/bad-capability.qns" \
  bs=1 seek=69 count=4 conv=notrunc status=none
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/bad-capability.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000000100 \
  >"$TMP/bad-capability.out" 2>"$TMP/bad-capability.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5009' "$TMP/bad-capability.err"

echo "=== WRONG SOURCE REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/ghz3.qn \
  "$TMP/model.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000000100 \
  >"$TMP/source.out" 2>"$TMP/source.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5008' "$TMP/source.err"

echo "=== EXPIRED TOKEN REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 2000003601 \
  >"$TMP/expired.out" 2>"$TMP/expired.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5006' "$TMP/expired.err"

echo "=== FUTURE TOKEN REJECTION ==="
set +e
"$BIN" approval verify-ed25519 \
  examples/approval_model.qn \
  "$TMP/model.qns" \
  --public-key "$TMP/ed-public-a.key" \
  --now 1999999999 \
  >"$TMP/future.out" 2>"$TMP/future.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5007' "$TMP/future.err"

echo "=== EQUAL TIMESTAMP REJECTION ==="
set +e
"$BIN" approval issue-ed25519 \
  examples/approval_model.qn \
  model.exec \
  --private-key "$TMP/ed-private-a.key" \
  --issued-at 2000000000 \
  --expires-at 2000000000 \
  -o "$TMP/equal.qns" \
  >"$TMP/equal.out" 2>"$TMP/equal.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/equal.err"

echo "=== UINT64 TIMESTAMP BOUNDARY REJECTION ==="
set +e
"$BIN" approval issue-ed25519 \
  examples/approval_model.qn \
  model.exec \
  --private-key "$TMP/ed-private-a.key" \
  --issued-at 18446744073709551614 \
  --expires-at 18446744073709551615 \
  -o "$TMP/overflow.qns" \
  >"$TMP/overflow.out" 2>"$TMP/overflow.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5004' "$TMP/overflow.err"

echo "=== BLOCKED CAPABILITY CANNOT BE SIGNED ==="
set +e
"$BIN" approval issue-ed25519 \
  examples/blocked_shell.qn \
  shell.exec \
  --private-key "$TMP/ed-private-a.key" \
  --issued-at 2000000000 \
  --expires-at 2000003600 \
  -o "$TMP/shell.qns" \
  >"$TMP/shell.out" 2>"$TMP/shell.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E5010' "$TMP/shell.err"

echo "=== SAFE PROGRAM WITHOUT APPROVAL ==="
"$BIN" run examples/ghz3.qn > "$TMP/ghz.out"
grep -q '^approval_scheme=none$' "$TMP/ghz.out"
grep -q '^approval_revocation=not-applicable$'   "$TMP/ghz.out"
grep -q '^approval_token_revoked=not-applicable$'   "$TMP/ghz.out"
grep -q '^approval_issuer_revoked=not-applicable$'   "$TMP/ghz.out"
grep -q '^approval_replay=not-applicable$'   "$TMP/ghz.out"
if grep -Eq '^\|001>=|^\|010>=|^\|011>=|^\|100>=|^\|101>=|^\|110>=' \
  "$TMP/ghz.out"; then
  echo "FAIL: invalid GHZ state"
  exit 1
fi

echo "=== DETERMINISTIC QBC ==="
"$BIN" build examples/approval_model.qn -o "$TMP/a.qbc" >/dev/null
"$BIN" build examples/approval_model.qn -o "$TMP/b.qbc" >/dev/null
cmp "$TMP/a.qbc" "$TMP/b.qbc"

echo "PASS: QBIT_NOVA_REAL_V3D_COMPUTE_V06_STEP3"
echo "PASS: QBIT_NOVA_GPU_ADAPTER_CONTRACT_V06_STEP2"
echo "PASS: QBIT_NOVA_RECEIPT_EVIDENCE_V052_STEP8"
echo "PASS: QBIT_NOVA_REVOCATION_EXECUTION_WIRING_V052_STEP7"
echo "PASS: QBIT_NOVA_REPLAY_EXECUTION_BOUNDARY_V052_STEP5"
echo "PASS: QBIT_NOVA_TRUSTED_ISSUER_CLI_V052_STEP3"
echo "PASS: QBIT_NOVA_OPENSSL_ED25519_APPROVAL_TEST_SUITE_V05"
