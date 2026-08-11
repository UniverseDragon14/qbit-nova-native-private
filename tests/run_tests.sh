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

echo "=== QVM GPU ROUTING ==="
"$BIN" run examples/ghz3.qn \
  --backend cpu \
  --shots 128 \
  --seed 4242 \
  --receipt "$TMP/qvm-cpu.json" \
  > "$TMP/qvm-cpu.out"
grep -q '^qvm_backend_schema=QBIT_NOVA_QVM_GPU_ROUTING_V06$' \
  "$TMP/qvm-cpu.out"
grep -q '^qvm_requested_backend=cpu$' "$TMP/qvm-cpu.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/qvm-cpu.out"
grep -q '^qvm_selection_reason=explicit-cpu$' "$TMP/qvm-cpu.out"
grep -q '^qvm_operation=quantum-state-simulation$' \
  "$TMP/qvm-cpu.out"
grep -q '^qvm_gpu_eligible=false$' "$TMP/qvm-cpu.out"
grep -q '^qvm_gpu_execution_attempted=false$' "$TMP/qvm-cpu.out"
grep -q '^qvm_gpu_execution_completed=false$' "$TMP/qvm-cpu.out"
grep -q '^qvm_cpu_fallback=false$' "$TMP/qvm-cpu.out"
grep -q '"qvm_requested_backend": "cpu"' "$TMP/qvm-cpu.json"
grep -q '"qvm_selected_backend": "cpu"' "$TMP/qvm-cpu.json"
grep -q '"qvm_cpu_fallback": false' "$TMP/qvm-cpu.json"

"$BIN" run examples/ghz3.qn \
  --backend auto \
  --shots 128 \
  --seed 4242 \
  --receipt "$TMP/qvm-auto.json" \
  > "$TMP/qvm-auto.out"
grep -q '^qvm_requested_backend=auto$' "$TMP/qvm-auto.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/qvm-auto.out"
grep -q '^qvm_selection_reason=qvm-operation-not-gpu-eligible$' \
  "$TMP/qvm-auto.out"
grep -q '^qvm_gpu_eligible=false$' "$TMP/qvm-auto.out"
grep -q '^qvm_gpu_execution_attempted=false$' "$TMP/qvm-auto.out"
grep -q '^qvm_gpu_execution_completed=false$' "$TMP/qvm-auto.out"
grep -q '^qvm_cpu_fallback=true$' "$TMP/qvm-auto.out"
grep -q '"qvm_requested_backend": "auto"' "$TMP/qvm-auto.json"
grep -q '"qvm_selected_backend": "cpu"' "$TMP/qvm-auto.json"
grep -q '"qvm_cpu_fallback": true' "$TMP/qvm-auto.json"

grep '^|' "$TMP/qvm-cpu.out" > "$TMP/qvm-cpu.hist"
grep '^|' "$TMP/qvm-auto.out" > "$TMP/qvm-auto.hist"
cmp "$TMP/qvm-cpu.hist" "$TMP/qvm-auto.hist"

set +e
"$BIN" run examples/ghz3.qn \
  --backend vulkan \
  --receipt "$TMP/qvm-vulkan.json" \
  >"$TMP/qvm-vulkan.out" \
  2>"$TMP/qvm-vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7201' "$TMP/qvm-vulkan.err"
test ! -s "$TMP/qvm-vulkan.out"
test ! -e "$TMP/qvm-vulkan.json"

"$BIN" build examples/ghz3.qn -o "$TMP/qvm-route.qbc" >/dev/null
"$BIN" exec "$TMP/qvm-route.qbc" \
  --backend auto \
  --shots 128 \
  --seed 4242 \
  > "$TMP/qvm-exec-auto.out"
grep -q '^qvm_requested_backend=auto$' "$TMP/qvm-exec-auto.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/qvm-exec-auto.out"
grep -q '^qvm_cpu_fallback=true$' "$TMP/qvm-exec-auto.out"

echo "=== NATIVE BOUNDED GPU OPERATION ==="
"$BIN" qir examples/vector_add_u32.qn \
  > "$TMP/vector-qir.out"
grep -q '^qubits=0$' "$TMP/vector-qir.out"
grep -q '^capabilities=evidence.emit,compute.u32_vector_add$' \
  "$TMP/vector-qir.out"
grep -q 'U32.VECTOR.ADD' "$TMP/vector-qir.out"
grep -q 'u32vec<256> fixed_input_a' "$TMP/vector-qir.out"
grep -q 'u32vec<256> sum' "$TMP/vector-qir.out"

"$BIN" build examples/vector_add_u32.qn \
  -o "$TMP/vector-a.qbc" >/dev/null
"$BIN" build examples/vector_add_u32.qn \
  -o "$TMP/vector-b.qbc" >/dev/null
cmp "$TMP/vector-a.qbc" "$TMP/vector-b.qbc"
test "$(stat -c '%s' "$TMP/vector-a.qbc")" -eq 104

test "$({
  od -An -tu1 -j 80 -N 1 "$TMP/vector-a.qbc" |
    tr -d '[:space:]'
})" = "80"

"$BIN" run examples/vector_add_u32.qn \
  --backend cpu \
  --receipt "$TMP/vector-cpu.json" \
  > "$TMP/vector-cpu.out"
grep -q '^QBIT_NOVA_NATIVE_COMPUTE_RUN_V06$' \
  "$TMP/vector-cpu.out"
grep -q '^boundary=native_bounded_compute$' \
  "$TMP/vector-cpu.out"
grep -q '^qvm_requested_backend=cpu$' "$TMP/vector-cpu.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/vector-cpu.out"
grep -q '^qvm_selection_reason=explicit-cpu$' \
  "$TMP/vector-cpu.out"
grep -q '^qvm_operation=bounded-uint32-vector-add$' \
  "$TMP/vector-cpu.out"
grep -q '^qvm_gpu_eligible=true$' "$TMP/vector-cpu.out"
grep -q '^qvm_gpu_execution_attempted=false$' \
  "$TMP/vector-cpu.out"
grep -q '^qvm_gpu_execution_completed=false$' \
  "$TMP/vector-cpu.out"
grep -q '^qvm_cpu_fallback=false$' "$TMP/vector-cpu.out"
grep -q '^element_count=256$' "$TMP/vector-cpu.out"
grep -q '^cpu_reference_validated=true$' "$TMP/vector-cpu.out"
grep -q '^result_match=true$' "$TMP/vector-cpu.out"
grep -q '"marker": "QBIT_NOVA_NATIVE_COMPUTE_RECEIPT_V06"' \
  "$TMP/vector-cpu.json"
grep -q '"qvm_selected_backend": "cpu"' \
  "$TMP/vector-cpu.json"
grep -q '"result_match": true' "$TMP/vector-cpu.json"

"$BIN" run examples/vector_add_u32.qn \
  --backend auto \
  --receipt "$TMP/vector-auto.json" \
  > "$TMP/vector-auto.out"
grep -q '^qvm_requested_backend=auto$' "$TMP/vector-auto.out"
grep -q '^qvm_selected_backend=vulkan$' "$TMP/vector-auto.out"
grep -q '^qvm_selection_reason=verified-v3d-auto$' \
  "$TMP/vector-auto.out"
grep -q '^qvm_gpu_eligible=true$' "$TMP/vector-auto.out"
grep -q '^qvm_gpu_execution_attempted=true$' \
  "$TMP/vector-auto.out"
grep -q '^qvm_gpu_execution_completed=true$' \
  "$TMP/vector-auto.out"
grep -q '^qvm_cpu_fallback=false$' "$TMP/vector-auto.out"
grep -q '^hardware_device=V3D ' "$TMP/vector-auto.out"
grep -q '^hardware_vendor_id=0x14e4$' "$TMP/vector-auto.out"
grep -q '^cpu_reference_validated=true$' "$TMP/vector-auto.out"
grep -q '^result_match=true$' "$TMP/vector-auto.out"
grep -q '"qvm_selected_backend": "vulkan"' \
  "$TMP/vector-auto.json"
grep -q '"qvm_gpu_execution_completed": true' \
  "$TMP/vector-auto.json"

"$BIN" run examples/vector_add_u32.qn \
  --backend vulkan \
  --receipt "$TMP/vector-vulkan.json" \
  > "$TMP/vector-vulkan.out"
grep -q '^qvm_requested_backend=vulkan$' \
  "$TMP/vector-vulkan.out"
grep -q '^qvm_selected_backend=vulkan$' \
  "$TMP/vector-vulkan.out"
grep -q '^qvm_selection_reason=explicit-verified-v3d$' \
  "$TMP/vector-vulkan.out"
grep -q '^qvm_gpu_execution_completed=true$' \
  "$TMP/vector-vulkan.out"
grep -q '^result_match=true$' "$TMP/vector-vulkan.out"

CPU_OUTPUT_SHA="$({
  sed -n 's/^output_sha256=//p' "$TMP/vector-cpu.out"
})"
AUTO_OUTPUT_SHA="$({
  sed -n 's/^output_sha256=//p' "$TMP/vector-auto.out"
})"
VULKAN_OUTPUT_SHA="$({
  sed -n 's/^output_sha256=//p' "$TMP/vector-vulkan.out"
})"

test "${#CPU_OUTPUT_SHA}" -eq 64
test "$CPU_OUTPUT_SHA" = "$AUTO_OUTPUT_SHA"
test "$CPU_OUTPUT_SHA" = "$VULKAN_OUTPUT_SHA"

"$BIN" exec "$TMP/vector-a.qbc" \
  --backend auto \
  --receipt "$TMP/vector-exec-auto.json" \
  > "$TMP/vector-exec-auto.out"
grep -q '^qvm_selected_backend=vulkan$' \
  "$TMP/vector-exec-auto.out"
grep -q '^qvm_gpu_execution_completed=true$' \
  "$TMP/vector-exec-auto.out"
grep -q '^result_match=true$' "$TMP/vector-exec-auto.out"

set +e
"$BIN" run examples/vector_add_u32.qn \
  --backend cpu \
  --shots 2 \
  --receipt "$TMP/vector-invalid-shots.json" \
  >"$TMP/vector-invalid-shots.out" \
  2>"$TMP/vector-invalid-shots.err"
STATUS=$?
set -e
test "$STATUS" -eq 4
grep -q 'QN-E7410' "$TMP/vector-invalid-shots.err"
test ! -e "$TMP/vector-invalid-shots.json"

for OPTION in --shots --seed
do
  set +e
  "$BIN" run examples/vector_add_u32.qn \
    --backend cpu \
    "$OPTION" 0 \
    >"$TMP/vector-explicit-zero.out" \
    2>"$TMP/vector-explicit-zero.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 4
  grep -q 'QN-E7410' "$TMP/vector-explicit-zero.err"
  test ! -s "$TMP/vector-explicit-zero.out"
done

for BAD_SOURCE in \
  tests/bad_gpu_mixed_quantum.qn \
  tests/bad_gpu_duplicate_operation.qn \
  tests/bad_gpu_missing_emit.qn \
  tests/bad_gpu_shots.qn \
  tests/bad_gpu_extra_capability.qn
do
  set +e
  "$BIN" check "$BAD_SOURCE" \
    >"$TMP/bad-gpu.out" \
    2>"$TMP/bad-gpu.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 5
done

set +e
"$BIN" check tests/bad_gpu_mixed_quantum.qn \
  >/dev/null 2>"$TMP/bad-gpu-mixed.err"
MIXED_STATUS=$?
"$BIN" check tests/bad_gpu_duplicate_operation.qn \
  >/dev/null 2>"$TMP/bad-gpu-duplicate.err"
DUPLICATE_STATUS=$?
"$BIN" check tests/bad_gpu_missing_emit.qn \
  >/dev/null 2>"$TMP/bad-gpu-missing-emit.err"
MISSING_EMIT_STATUS=$?
"$BIN" check tests/bad_gpu_shots.qn \
  >/dev/null 2>"$TMP/bad-gpu-shots.err"
SHOTS_STATUS=$?
"$BIN" check tests/bad_gpu_extra_capability.qn \
  >/dev/null 2>"$TMP/bad-gpu-capability.err"
CAPABILITY_STATUS=$?
set -e

test "$MIXED_STATUS" -eq 5
test "$DUPLICATE_STATUS" -eq 5
test "$MISSING_EMIT_STATUS" -eq 5
test "$SHOTS_STATUS" -eq 5
test "$CAPABILITY_STATUS" -eq 5
grep -q 'QN-E7401' "$TMP/bad-gpu-mixed.err"
grep -q 'QN-E7402' "$TMP/bad-gpu-duplicate.err"
grep -q 'QN-E7404' "$TMP/bad-gpu-missing-emit.err"
grep -q 'QN-E7403' "$TMP/bad-gpu-shots.err"
grep -q 'QN-E7405' "$TMP/bad-gpu-capability.err"

python3 - "$TMP/vector-a.qbc" "$TMP/vector-tampered.qbc" <<'PY'
from pathlib import Path
import sys

source = bytearray(Path(sys.argv[1]).read_bytes())
source[84:88] = (255).to_bytes(4, "little")
Path(sys.argv[2]).write_bytes(source)
PY

set +e
"$BIN" exec "$TMP/vector-tampered.qbc" \
  --backend cpu \
  >"$TMP/vector-tampered.out" \
  2>"$TMP/vector-tampered.err"
STATUS=$?
set -e
test "$STATUS" -eq 6
grep -q 'QN-E7406' "$TMP/vector-tampered.err"
test ! -s "$TMP/vector-tampered.out"

echo "BOUNDED_GPU_CPU_V3D_RESULT_MATCH=PASS"

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

echo "=== QVM VULKAN REJECTS BEFORE REPLAY ==="
set +e
"$BIN" run examples/approval_model.qn \
  --signed-approval-file "$TMP/model.qns" \
  --trust-store-file "$TMP/trust-a.qnts" \
  --replay-ledger-file "$TMP/replay-qvm-vulkan.qnrl" \
  --revocation-store-file "$TMP/revocation-empty.qnrv" \
  --backend vulkan \
  --now 2000000100 \
  --receipt "$TMP/qvm-vulkan-signed.json" \
  >"$TMP/qvm-vulkan-signed.out" \
  2>"$TMP/qvm-vulkan-signed.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7201' "$TMP/qvm-vulkan-signed.err"
test ! -e "$TMP/replay-qvm-vulkan.qnrl"
test ! -e "$TMP/qvm-vulkan-signed.json"
test ! -s "$TMP/qvm-vulkan-signed.out"

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
grep -q '^qvm_requested_backend=cpu$' "$TMP/ghz.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/ghz.out"
grep -q '^qvm_selection_reason=default-cpu$' "$TMP/ghz.out"
grep -q '^qvm_gpu_eligible=false$' "$TMP/ghz.out"
grep -q '^qvm_gpu_execution_attempted=false$' "$TMP/ghz.out"
grep -q '^qvm_gpu_execution_completed=false$' "$TMP/ghz.out"
grep -q '^qvm_cpu_fallback=false$' "$TMP/ghz.out"
if grep -Eq '^\|001>=|^\|010>=|^\|011>=|^\|100>=|^\|101>=|^\|110>=' \
  "$TMP/ghz.out"; then
  echo "FAIL: invalid GHZ state"
  exit 1
fi

echo "=== TYPED U32 SCALAR LANGUAGE CORE ==="
"$BIN" check examples/u32_scalar.qn > "$TMP/u32-check.out"
grep -q 'PASS: QBIT_NOVA_NATIVE_CHECK_V01' "$TMP/u32-check.out"
grep -q '^qubits=0$' "$TMP/u32-check.out"
grep -q '^instructions=5$' "$TMP/u32-check.out"

"$BIN" qir examples/u32_scalar.qn > "$TMP/u32-qir.out"
grep -q '^u32_scalars=3$' "$TMP/u32-qir.out"
grep -q 'U32.CONST.*10.*u32 a@0' "$TMP/u32-qir.out"
grep -q 'U32.CONST.*20.*u32 b@1' "$TMP/u32-qir.out"
grep -q 'U32.ADD.*u32 a@0.*u32 b@1.*u32 sum@2' "$TMP/u32-qir.out"
grep -q 'U32.EMIT.*u32 sum@2' "$TMP/u32-qir.out"

"$BIN" build examples/u32_scalar.qn -o "$TMP/u32-a.qbc" >/dev/null
"$BIN" build examples/u32_scalar.qn -o "$TMP/u32-b.qbc" >/dev/null
cmp "$TMP/u32-a.qbc" "$TMP/u32-b.qbc"
test "$(stat -c '%s' "$TMP/u32-a.qbc")" -eq 120
test "$(od -An -tu1 -j 4 -N 1 "$TMP/u32-a.qbc" | tr -d '[:space:]')" = 4
test "$(od -An -tu1 -j 76 -N 1 "$TMP/u32-a.qbc" | tr -d '[:space:]')" = 3

"$BIN" run examples/u32_scalar.qn \
  --backend cpu --receipt "$TMP/u32-cpu.json" > "$TMP/u32-cpu.out"
grep -q '^QBIT_NOVA_NATIVE_SCALAR_RUN_V07$' "$TMP/u32-cpu.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/u32-cpu.out"
grep -q '^qvm_cpu_fallback=false$' "$TMP/u32-cpu.out"
grep -q '^emitted_u32=30$' "$TMP/u32-cpu.out"
grep -q '"marker": "QBIT_NOVA_NATIVE_SCALAR_RECEIPT_V07"' "$TMP/u32-cpu.json"
grep -q '"emitted_u32": 30' "$TMP/u32-cpu.json"

"$BIN" exec "$TMP/u32-a.qbc" --backend auto > "$TMP/u32-auto.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/u32-auto.out"
grep -q '^qvm_selection_reason=scalar-operation-not-gpu-eligible$' "$TMP/u32-auto.out"
grep -q '^qvm_cpu_fallback=true$' "$TMP/u32-auto.out"
grep -q '^emitted_u32=30$' "$TMP/u32-auto.out"

"$BIN" run tests/u32_scalar_overflow.qn --backend cpu \
  > "$TMP/u32-overflow.out"
grep -q '^emitted_u32=0$' "$TMP/u32-overflow.out"

for CASE in \
  bad_u32_duplicate \
  bad_u32_unknown \
  bad_u32_missing_emit \
  bad_u32_mixed_quantum \
  bad_u32_literal_overflow \
  bad_u32_shots
do
  set +e
  "$BIN" check "tests/${CASE}.qn" \
    >"$TMP/${CASE}.out" 2>"$TMP/${CASE}.err"
  STATUS=$?
  set -e
  test "$STATUS" -ne 0
  test ! -s "$TMP/${CASE}.out"
done

grep -q 'QN-E7501' "$TMP/bad_u32_duplicate.err"
grep -q 'QN-E7504' "$TMP/bad_u32_unknown.err"
grep -q 'QN-E7508' "$TMP/bad_u32_missing_emit.err"
grep -q 'QN-E7503' "$TMP/bad_u32_mixed_quantum.err"
grep -q 'QN-E7500' "$TMP/bad_u32_literal_overflow.err"
grep -q 'QN-E7505' "$TMP/bad_u32_shots.err"

for OPTION in --shots --seed
do
  set +e
  "$BIN" run examples/u32_scalar.qn --backend cpu "$OPTION" 1 \
    >"$TMP/u32-option.out" 2>"$TMP/u32-option.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 4
  grep -q 'QN-E7510' "$TMP/u32-option.err"
  test ! -s "$TMP/u32-option.out"
done

set +e
"$BIN" run examples/u32_scalar.qn --backend vulkan \
  >"$TMP/u32-vulkan.out" 2>"$TMP/u32-vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7201' "$TMP/u32-vulkan.err"
test ! -s "$TMP/u32-vulkan.out"

echo "TYPED_U32_SCALAR_PIPELINE=PASS"

echo "=== U32 SUB MUL DIV ARITHMETIC EXTENSION ==="

"$BIN" qir examples/u32_scalar_sub.qn > "$TMP/u32-sub-qir.out"
grep -q 'U32.SUB.*u32 a@0.*u32 b@1.*u32 diff@2' \
  "$TMP/u32-sub-qir.out"

"$BIN" qir examples/u32_scalar_mul.qn > "$TMP/u32-mul-qir.out"
grep -q 'U32.MUL.*u32 a@0.*u32 b@1.*u32 product@2' \
  "$TMP/u32-mul-qir.out"

"$BIN" qir examples/u32_scalar_div.qn > "$TMP/u32-div-qir.out"
grep -q 'U32.DIV.*u32 a@0.*u32 b@1.*u32 quotient@2' \
  "$TMP/u32-div-qir.out"

for SPEC in \
  'sub 54 18 7d8e29fa389a36cca29bc0f07a7892dddd6f9070b9e33d12dce8ce3569f81810' \
  'mul 55 42 e8a4b2ee7ede79a3afb332b5b6cc3d952a65fd8cffb897f5d18016577c33d7cc' \
  'div 56 25 0623ccb9b1619bd388284a438034d8cb6431964ba727d8b1c450303105735488'
do
  set -- $SPEC
  NAME="$1"
  OPCODE="$2"
  VALUE="$3"
  OUTPUT_SHA="$4"
  SOURCE="examples/u32_scalar_${NAME}.qn"

  "$BIN" check "$SOURCE" > "$TMP/u32-${NAME}-check.out"
  grep -q '^instructions=5$' "$TMP/u32-${NAME}-check.out"

  "$BIN" build "$SOURCE" -o "$TMP/u32-${NAME}-a.qbc" >/dev/null
  "$BIN" build "$SOURCE" -o "$TMP/u32-${NAME}-b.qbc" >/dev/null
  cmp "$TMP/u32-${NAME}-a.qbc" "$TMP/u32-${NAME}-b.qbc"
  test "$(stat -c '%s' "$TMP/u32-${NAME}-a.qbc")" -eq 120
  test "$(od -An -tu1 -j 4 -N 1 "$TMP/u32-${NAME}-a.qbc" | tr -d '[:space:]')" = 4
  test "$(od -An -tu1 -j 76 -N 1 "$TMP/u32-${NAME}-a.qbc" | tr -d '[:space:]')" = 3
  test "$(od -An -tx1 -j 96 -N 1 "$TMP/u32-${NAME}-a.qbc" | tr -d '[:space:]')" = "$OPCODE"

  "$BIN" run "$SOURCE" --backend cpu \
    --receipt "$TMP/u32-${NAME}-cpu.json" \
    > "$TMP/u32-${NAME}-cpu.out"

  grep -q '^QBIT_NOVA_NATIVE_SCALAR_RUN_V07$' \
    "$TMP/u32-${NAME}-cpu.out"
  grep -q '^arithmetic_ops=add,sub,mul,div$' \
    "$TMP/u32-${NAME}-cpu.out"
  grep -q '^division_semantics=unsigned-integer-truncate$' \
    "$TMP/u32-${NAME}-cpu.out"
  grep -q '^division_by_zero=fail-closed-QN-E7517$' \
    "$TMP/u32-${NAME}-cpu.out"
  grep -q "^emitted_u32=${VALUE}$" "$TMP/u32-${NAME}-cpu.out"
  grep -q "^output_sha256=${OUTPUT_SHA}$" \
    "$TMP/u32-${NAME}-cpu.out"

  grep -q '"arithmetic_ops": "add,sub,mul,div"' \
    "$TMP/u32-${NAME}-cpu.json"
  grep -q '"division_semantics": "unsigned-integer-truncate"' \
    "$TMP/u32-${NAME}-cpu.json"
  grep -q '"division_by_zero": "fail-closed-QN-E7517"' \
    "$TMP/u32-${NAME}-cpu.json"
  grep -q "\"emitted_u32\": ${VALUE}" \
    "$TMP/u32-${NAME}-cpu.json"

  "$BIN" exec "$TMP/u32-${NAME}-a.qbc" --backend auto \
    --receipt "$TMP/u32-${NAME}-exec.json" \
    > "$TMP/u32-${NAME}-exec.out"
  grep -q '^qvm_selected_backend=cpu$' \
    "$TMP/u32-${NAME}-exec.out"
  grep -q '^qvm_selection_reason=scalar-operation-not-gpu-eligible$' \
    "$TMP/u32-${NAME}-exec.out"
  grep -q '^qvm_cpu_fallback=true$' \
    "$TMP/u32-${NAME}-exec.out"
  grep -q "^emitted_u32=${VALUE}$" \
    "$TMP/u32-${NAME}-exec.out"
done

"$BIN" run tests/u32_scalar_sub_underflow.qn --backend cpu \
  > "$TMP/u32-sub-underflow.out"
grep -q '^emitted_u32=4294967295$' \
  "$TMP/u32-sub-underflow.out"
grep -q '^output_sha256=ad95131bc0b799c0b1af477fb14fcf26a6a9f76079e48bf090acb7e8367bfd0e$' \
  "$TMP/u32-sub-underflow.out"

"$BIN" run tests/u32_scalar_mul_overflow.qn --backend cpu \
  > "$TMP/u32-mul-overflow.out"
grep -q '^emitted_u32=4294967294$' \
  "$TMP/u32-mul-overflow.out"
grep -q '^output_sha256=b4248c210a2905b94345e1a8414d0e12efcfb2f4f0f2397159a71283397a0ccd$' \
  "$TMP/u32-mul-overflow.out"

"$BIN" run tests/u32_scalar_div_truncation.qn --backend cpu \
  > "$TMP/u32-div-truncation.out"
grep -q '^emitted_u32=3$' "$TMP/u32-div-truncation.out"
grep -q '^output_sha256=9d9f290527a6be626a8f5985b26e19b237b44872b03631811df4416fc1713178$' \
  "$TMP/u32-div-truncation.out"

"$BIN" build tests/u32_scalar_div_zero.qn \
  -o "$TMP/u32-div-zero.qbc" >/dev/null

set +e
"$BIN" run tests/u32_scalar_div_zero.qn --backend cpu \
  > "$TMP/u32-div-zero-run.out" \
  2> "$TMP/u32-div-zero-run.err"
DIV_RUN_STATUS=$?

"$BIN" exec "$TMP/u32-div-zero.qbc" --backend auto \
  > "$TMP/u32-div-zero-exec.out" \
  2> "$TMP/u32-div-zero-exec.err"
DIV_EXEC_STATUS=$?
set -e

test "$DIV_RUN_STATUS" -eq 7
test "$DIV_EXEC_STATUS" -eq 7
test ! -s "$TMP/u32-div-zero-run.out"
test ! -s "$TMP/u32-div-zero-exec.out"
grep -q 'QN-E7517' "$TMP/u32-div-zero-run.err"
grep -q 'QN-E7517' "$TMP/u32-div-zero-exec.err"

"$BIN" build examples/u32_scalar.qn \
  -o "$TMP/u32-step1-regression.qbc" >/dev/null
test "$(sha256sum "$TMP/u32-step1-regression.qbc" | awk '{print $1}')" = \
  '9c7c0638cf508ad5c690f9764395a40679f183ef4d6f5681fb101cb0e025aa12'

"$BIN" build examples/vector_add_u32.qn \
  -o "$TMP/u32-stage6-vector-regression.qbc" >/dev/null
test "$(sha256sum "$TMP/u32-stage6-vector-regression.qbc" | awk '{print $1}')" = \
  '9f9a624a88200847595c3a5499dc03fe4811bef1676e988fac4ce53a8448642f'

echo "U32_ARITHMETIC_PIPELINE=PASS"

echo "=== U32 COMPARISONS + NATIVE BOOL ==="

TRUE_SHA='4bf5122f344554c53bde2ebb8cd2b7e3d1600ad631c385a5d7cce23c7785459a'
FALSE_SHA='6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d'

for SPEC in \
  'eq EQ 57' \
  'ne NE 58' \
  'lt LT 59' \
  'le LE 5a' \
  'gt GT 5b' \
  'ge GE 5c'
do
  set -- $SPEC
  NAME="$1"
  QIR_NAME="$2"
  OPCODE="$3"
  SOURCE="examples/u32_compare_${NAME}.qn"

  "$BIN" qir "$SOURCE" > "$TMP/u32-cmp-${NAME}-qir.out"
  grep -q '^QBIT_NOVA_TYPED_QIR_V03$' \
    "$TMP/u32-cmp-${NAME}-qir.out"
  grep -q '^scalar_slots=3$' "$TMP/u32-cmp-${NAME}-qir.out"
  grep -q '^u32_scalars=2$' "$TMP/u32-cmp-${NAME}-qir.out"
  grep -q '^bool_scalars=1$' "$TMP/u32-cmp-${NAME}-qir.out"
  grep -q "U32.${QIR_NAME}.*u32 a@0.*u32 b@1.*bool result@2" \
    "$TMP/u32-cmp-${NAME}-qir.out"
  grep -q 'BOOL.EMIT.*bool result@2' \
    "$TMP/u32-cmp-${NAME}-qir.out"

  "$BIN" check "$SOURCE" > "$TMP/u32-cmp-${NAME}-check.out"
  grep -q '^instructions=5$' "$TMP/u32-cmp-${NAME}-check.out"

  "$BIN" build "$SOURCE" -o "$TMP/u32-cmp-${NAME}-a.qbc" >/dev/null
  "$BIN" build "$SOURCE" -o "$TMP/u32-cmp-${NAME}-b.qbc" >/dev/null
  cmp "$TMP/u32-cmp-${NAME}-a.qbc" "$TMP/u32-cmp-${NAME}-b.qbc"
  test "$(stat -c '%s' "$TMP/u32-cmp-${NAME}-a.qbc")" -eq 128
  test "$(od -An -tu1 -j 4 -N 1 "$TMP/u32-cmp-${NAME}-a.qbc" | tr -d '[:space:]')" = 5
  test "$(od -An -tu1 -j 76 -N 1 "$TMP/u32-cmp-${NAME}-a.qbc" | tr -d '[:space:]')" = 3
  test "$(od -An -tx1 -j 80 -N 8 "$TMP/u32-cmp-${NAME}-a.qbc" | tr -d '[:space:]')" = '0400000000000000'
  test "$(od -An -tx1 -j 104 -N 1 "$TMP/u32-cmp-${NAME}-a.qbc" | tr -d '[:space:]')" = "$OPCODE"
  test "$(od -An -tx1 -j 112 -N 1 "$TMP/u32-cmp-${NAME}-a.qbc" | tr -d '[:space:]')" = '5d'

  "$BIN" run "$SOURCE" --backend cpu \
    --receipt "$TMP/u32-cmp-${NAME}-cpu.json" \
    > "$TMP/u32-cmp-${NAME}-cpu.out"

  grep -q '^QBIT_NOVA_NATIVE_TYPED_SCALAR_RUN_V07_STEP3$' \
    "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q '^qvm_operation=typed-scalar-program$' \
    "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q '^comparison_ops=eq,ne,lt,le,gt,ge$' \
    "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q '^comparison_operand_type=u32$' \
    "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q '^comparison_result_type=bool$' \
    "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q '^output_type=bool$' \
    "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q '^emitted_bool=true$' "$TMP/u32-cmp-${NAME}-cpu.out"
  grep -q "^output_sha256=${TRUE_SHA}$" \
    "$TMP/u32-cmp-${NAME}-cpu.out"

  grep -q '"marker": "QBIT_NOVA_NATIVE_TYPED_SCALAR_RECEIPT_V07_STEP3"' \
    "$TMP/u32-cmp-${NAME}-cpu.json"
  grep -q '"comparison_result_type": "bool"' \
    "$TMP/u32-cmp-${NAME}-cpu.json"
  grep -q '"emitted_bool": true' \
    "$TMP/u32-cmp-${NAME}-cpu.json"

  "$BIN" exec "$TMP/u32-cmp-${NAME}-a.qbc" --backend auto \
    > "$TMP/u32-cmp-${NAME}-exec.out"
  grep -q '^qvm_selected_backend=cpu$' \
    "$TMP/u32-cmp-${NAME}-exec.out"
  grep -q '^qvm_selection_reason=scalar-operation-not-gpu-eligible$' \
    "$TMP/u32-cmp-${NAME}-exec.out"
  grep -q '^qvm_operation=typed-scalar-program$' \
    "$TMP/u32-cmp-${NAME}-exec.out"
  grep -q '^emitted_bool=true$' \
    "$TMP/u32-cmp-${NAME}-exec.out"
done

"$BIN" run tests/u32_compare_false.qn --backend cpu \
  --receipt "$TMP/u32-cmp-false.json" \
  > "$TMP/u32-cmp-false.out"
grep -q '^emitted_bool=false$' "$TMP/u32-cmp-false.out"
grep -q "^output_sha256=${FALSE_SHA}$" "$TMP/u32-cmp-false.out"
grep -q '"emitted_bool": false' "$TMP/u32-cmp-false.json"

for CASE in \
  bad_bool_arithmetic \
  bad_bool_comparison \
  bad_compare_unknown
do
  set +e
  "$BIN" check "tests/${CASE}.qn" \
    >"$TMP/${CASE}.out" 2>"$TMP/${CASE}.err"
  STATUS=$?
  set -e
  test "$STATUS" -ne 0
  test ! -s "$TMP/${CASE}.out"
done

grep -q 'QN-E7521' "$TMP/bad_bool_arithmetic.err"
grep -q 'QN-E7522' "$TMP/bad_bool_comparison.err"
grep -q 'QN-E7504' "$TMP/bad_compare_unknown.err"

for OPTION in --shots --seed
do
  set +e
  "$BIN" run examples/u32_compare_lt.qn --backend cpu "$OPTION" 1 \
    >"$TMP/u32-cmp-option.out" 2>"$TMP/u32-cmp-option.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 4
  grep -q 'QN-E7510' "$TMP/u32-cmp-option.err"
  test ! -s "$TMP/u32-cmp-option.out"
done

set +e
"$BIN" run examples/u32_compare_lt.qn --backend vulkan \
  >"$TMP/u32-cmp-vulkan.out" 2>"$TMP/u32-cmp-vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7201' "$TMP/u32-cmp-vulkan.err"
test ! -s "$TMP/u32-cmp-vulkan.out"

"$BIN" run examples/u32_compare_lt.qn --backend cpu \
  --receipt "$TMP/u32-cmp-receipt-a.json" \
  > "$TMP/u32-cmp-receipt-a.out"
"$BIN" run examples/u32_compare_lt.qn --backend cpu \
  --receipt "$TMP/u32-cmp-receipt-b.json" \
  > "$TMP/u32-cmp-receipt-b.out"
cmp "$TMP/u32-cmp-receipt-a.json" "$TMP/u32-cmp-receipt-b.json"
cmp \
  <(grep -v '^receipt=' "$TMP/u32-cmp-receipt-a.out") \
  <(grep -v '^receipt=' "$TMP/u32-cmp-receipt-b.out")

"$BIN" build examples/u32_scalar.qn \
  -o "$TMP/u32-step1-step3-regression.qbc" >/dev/null
test "$(sha256sum "$TMP/u32-step1-step3-regression.qbc" | awk '{print $1}')" = \
  '9c7c0638cf508ad5c690f9764395a40679f183ef4d6f5681fb101cb0e025aa12'

"$BIN" build examples/u32_scalar_sub.qn \
  -o "$TMP/u32-step2-step3-regression.qbc" >/dev/null
test "$(sha256sum "$TMP/u32-step2-step3-regression.qbc" | awk '{print $1}')" = \
  'fa5174d59fce3a500a841472eb724216bf22b8c9cb30a604e7600c9e0d78e6d6'

"$BIN" build examples/vector_add_u32.qn \
  -o "$TMP/u32-stage6-step3-regression.qbc" >/dev/null
test "$(sha256sum "$TMP/u32-stage6-step3-regression.qbc" | awk '{print $1}')" = \
  '9f9a624a88200847595c3a5499dc03fe4811bef1676e988fac4ce53a8448642f'

echo "U32_COMPARISONS_BOOL_PIPELINE=PASS"

echo "=== NATIVE IF ELSE CONTROL FLOW ==="

U32_10_SHA='075de2b906dbd7066da008cab735bee896370154603579a50122f9b88545bd45'
U32_20_SHA='447e12701a0d03cf90a4ad7f02f1a045b35d284e26fe520440edb116d76bf700'
U32_17_SHA='84fc05949dc1e486652a4ed316afb6434e9437eb30b714594a1d0b4205776602'

"$BIN" qir examples/u32_if_else_true.qn > "$TMP/u32-if-true-qir.out"
grep -q '^QBIT_NOVA_TYPED_QIR_V04_STEP4$' "$TMP/u32-if-true-qir.out"
grep -q 'JUMP.IF.FALSE.*bool less@2.*-> 6' "$TMP/u32-if-true-qir.out"
grep -q 'JUMP.*-> 7' "$TMP/u32-if-true-qir.out"

"$BIN" check examples/u32_if_else_true.qn > "$TMP/u32-if-true-check.out"
grep -q '^instructions=8$' "$TMP/u32-if-true-check.out"

"$BIN" build examples/u32_if_else_true.qn \
  -o "$TMP/u32-if-true-a.qbc" >/dev/null
"$BIN" build examples/u32_if_else_true.qn \
  -o "$TMP/u32-if-true-b.qbc" >/dev/null
cmp "$TMP/u32-if-true-a.qbc" "$TMP/u32-if-true-b.qbc"
test "$(stat -c '%s' "$TMP/u32-if-true-a.qbc")" -eq 152
test "$(od -An -tu1 -j 4 -N 1 "$TMP/u32-if-true-a.qbc" | tr -d '[:space:]')" = 6
test "$(od -An -tu1 -j 6 -N 1 "$TMP/u32-if-true-a.qbc" | tr -d '[:space:]')" = 88
test "$(od -An -tu1 -j 76 -N 1 "$TMP/u32-if-true-a.qbc" | tr -d '[:space:]')" = 3
test "$(od -An -tx1 -j 80 -N 8 "$TMP/u32-if-true-a.qbc" | tr -d '[:space:]')" = '0400000000000000'
test "$(od -An -tx1 -j 112 -N 1 "$TMP/u32-if-true-a.qbc" | tr -d '[:space:]')" = '5e'
test "$(od -An -tx1 -j 128 -N 1 "$TMP/u32-if-true-a.qbc" | tr -d '[:space:]')" = '5f'

"$BIN" run examples/u32_if_else_true.qn --backend cpu \
  --receipt "$TMP/u32-if-true.json" \
  > "$TMP/u32-if-true.out"
grep -q '^QBIT_NOVA_NATIVE_TYPED_CONTROL_FLOW_RUN_V07_STEP4$' \
  "$TMP/u32-if-true.out"
grep -q '^boundary=native_typed_u32_bool_control_flow$' \
  "$TMP/u32-if-true.out"
grep -q '^qvm_operation=typed-control-flow-program$' \
  "$TMP/u32-if-true.out"
grep -q '^scalar_contract=typed-u32-bool-ifelse-v1$' \
  "$TMP/u32-if-true.out"
grep -q '^control_flow=if-else-forward-only$' \
  "$TMP/u32-if-true.out"
grep -q '^output_type=u32$' "$TMP/u32-if-true.out"
grep -q '^emitted_u32=10$' "$TMP/u32-if-true.out"
grep -q "^output_sha256=${U32_10_SHA}$" "$TMP/u32-if-true.out"
grep -q '"marker": "QBIT_NOVA_NATIVE_TYPED_CONTROL_FLOW_RECEIPT_V07_STEP4"' \
  "$TMP/u32-if-true.json"
grep -q '"control_flow": "if-else-forward-only"' \
  "$TMP/u32-if-true.json"

"$BIN" run examples/u32_if_else_false.qn --backend cpu \
  --receipt "$TMP/u32-if-false.json" \
  > "$TMP/u32-if-false.out"
grep -q '^emitted_u32=20$' "$TMP/u32-if-false.out"
grep -q "^output_sha256=${U32_20_SHA}$" "$TMP/u32-if-false.out"

"$BIN" run examples/u32_if_else_branch_local.qn --backend cpu \
  --receipt "$TMP/u32-if-local.json" \
  > "$TMP/u32-if-local.out"
grep -q '^emitted_u32=17$' "$TMP/u32-if-local.out"
grep -q "^output_sha256=${U32_17_SHA}$" "$TMP/u32-if-local.out"

for SPEC in \
  'bad_if_u32_condition QN-E7531' \
  'bad_if_unknown_condition QN-E7530' \
  'bad_if_missing_else QN-E7532' \
  'bad_if_missing_brace QN-E7533' \
  'bad_if_output_type_mismatch QN-E7538' \
  'bad_if_branch_without_emit QN-E7534' \
  'bad_if_nested QN-E7536'
do
  set -- $SPEC
  CASE="$1"
  CODE="$2"
  set +e
  "$BIN" check "tests/${CASE}.qn" \
    >"$TMP/${CASE}.out" 2>"$TMP/${CASE}.err"
  STATUS=$?
  set -e
  test "$STATUS" -ne 0
  test ! -s "$TMP/${CASE}.out"
  grep -q "$CODE" "$TMP/${CASE}.err"
done

for OPTION in --shots --seed
do
  set +e
  "$BIN" run examples/u32_if_else_true.qn --backend cpu "$OPTION" 1 \
    >"$TMP/u32-if-option.out" 2>"$TMP/u32-if-option.err"
  STATUS=$?
  set -e
  test "$STATUS" -eq 4
  grep -q 'QN-E7510' "$TMP/u32-if-option.err"
  test ! -s "$TMP/u32-if-option.out"
done

set +e
"$BIN" run examples/u32_if_else_true.qn --backend vulkan \
  >"$TMP/u32-if-vulkan.out" 2>"$TMP/u32-if-vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7201' "$TMP/u32-if-vulkan.err"
test ! -s "$TMP/u32-if-vulkan.out"

"$BIN" exec "$TMP/u32-if-true-a.qbc" --backend auto \
  > "$TMP/u32-if-exec-auto.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/u32-if-exec-auto.out"
grep -q '^qvm_selection_reason=scalar-operation-not-gpu-eligible$' \
  "$TMP/u32-if-exec-auto.out"
grep -q '^qvm_operation=typed-control-flow-program$' \
  "$TMP/u32-if-exec-auto.out"
grep -q '^emitted_u32=10$' "$TMP/u32-if-exec-auto.out"

"$BIN" run examples/u32_if_else_true.qn --backend cpu \
  --receipt "$TMP/u32-if-receipt-a.json" \
  > "$TMP/u32-if-receipt-a.out"
"$BIN" run examples/u32_if_else_true.qn --backend cpu \
  --receipt "$TMP/u32-if-receipt-b.json" \
  > "$TMP/u32-if-receipt-b.out"
cmp "$TMP/u32-if-receipt-a.json" "$TMP/u32-if-receipt-b.json"
cmp \
  <(grep -v '^receipt=' "$TMP/u32-if-receipt-a.out") \
  <(grep -v '^receipt=' "$TMP/u32-if-receipt-b.out")

cp "$TMP/u32-if-true-a.qbc" "$TMP/u32-if-backward.qbc"
python3 - "$TMP/u32-if-backward.qbc" <<'PY'
import struct
import sys
p = sys.argv[1]
with open(p, 'r+b') as f:
    f.seek(116)
    f.write(struct.pack('<I', 3))
PY
set +e
"$BIN" exec "$TMP/u32-if-backward.qbc" --backend cpu \
  >"$TMP/u32-if-backward.out" 2>"$TMP/u32-if-backward.err"
STATUS=$?
set -e
test "$STATUS" -eq 6
grep -q 'QN-E7541' "$TMP/u32-if-backward.err"
test ! -s "$TMP/u32-if-backward.out"

cp "$TMP/u32-if-true-a.qbc" "$TMP/u32-if-range.qbc"
python3 - "$TMP/u32-if-range.qbc" <<'PY'
import struct
import sys
p = sys.argv[1]
with open(p, 'r+b') as f:
    f.seek(116)
    f.write(struct.pack('<I', 99))
PY
set +e
"$BIN" exec "$TMP/u32-if-range.qbc" --backend cpu \
  >"$TMP/u32-if-range.out" 2>"$TMP/u32-if-range.err"
STATUS=$?
set -e
test "$STATUS" -eq 6
grep -q 'QN-E7541' "$TMP/u32-if-range.err"
test ! -s "$TMP/u32-if-range.out"

cp "$TMP/u32-if-true-a.qbc" "$TMP/u32-if-v5-tamper.qbc"
python3 - "$TMP/u32-if-v5-tamper.qbc" <<'PY'
import struct
import sys
p = sys.argv[1]
with open(p, 'r+b') as f:
    f.seek(4)
    f.write(struct.pack('<H', 5))
PY
set +e
"$BIN" exec "$TMP/u32-if-v5-tamper.qbc" --backend cpu \
  >"$TMP/u32-if-v5-tamper.out" 2>"$TMP/u32-if-v5-tamper.err"
STATUS=$?
set -e
test "$STATUS" -eq 6
grep -q 'QN-E7540' "$TMP/u32-if-v5-tamper.err"
test ! -s "$TMP/u32-if-v5-tamper.out"

# Frozen QBC compatibility: every previously frozen scalar/vector artifact
# remains byte-for-byte unchanged.
for SPEC in \
  'examples/u32_scalar.qn 9c7c0638cf508ad5c690f9764395a40679f183ef4d6f5681fb101cb0e025aa12' \
  'examples/u32_scalar_sub.qn fa5174d59fce3a500a841472eb724216bf22b8c9cb30a604e7600c9e0d78e6d6' \
  'examples/u32_scalar_mul.qn e1d0d2f4814dfbb914eaba3ed1330965a4b11bc2d76f6d4dbfe3af051e4079d8' \
  'examples/u32_scalar_div.qn 3bec38d21273192423967cc11520c4206e1409b245a37bd11c31cf0514af14d9' \
  'examples/u32_compare_eq.qn 00ee1eca5e1a469cf14446b571f4b0287dea322b799b0a747994383469397014' \
  'examples/u32_compare_ne.qn 2640ce131b1bf743665c69e319f5d83ecdb9467510fa99d66f2a4912b65e12fc' \
  'examples/u32_compare_lt.qn 8d5b31294afb0c6970e53e2cb6069d7edb9f5454ea94e0f968a4d88a32542293' \
  'examples/u32_compare_le.qn 053044f80ed3925bdca779e3eb4b6e8fea7cd6b65112b71849e2445570147d3b' \
  'examples/u32_compare_gt.qn 3b11c46a881e5b1bc0e37570e450ba898769f1d1782a7619c2440785eee34576' \
  'examples/u32_compare_ge.qn c25981d35b6ebc7d5ea281c6fc1cb527ebe9ed256bd41ff5c0c7d1c0d85d04aa' \
  'examples/vector_add_u32.qn 9f9a624a88200847595c3a5499dc03fe4811bef1676e988fac4ce53a8448642f'
do
  set -- $SPEC
  SOURCE="$1"
  EXPECTED_SHA="$2"
  OUT="$TMP/step4-regression-$(basename "$SOURCE" .qn).qbc"
  "$BIN" build "$SOURCE" -o "$OUT" >/dev/null
  test "$(sha256sum "$OUT" | awk '{print $1}')" = "$EXPECTED_SHA"
done

echo "NATIVE_IF_ELSE_CONTROL_FLOW=PASS"


echo "=== NATIVE BOUNDED REPEAT + EXPLICIT U32 MUTATION ==="

"$BIN" qir examples/u32_repeat_add.qn > "$TMP/u32-repeat-add-qir.out"
grep -q '^QBIT_NOVA_TYPED_QIR_V05_STEP5$' "$TMP/u32-repeat-add-qir.out"
grep -q '0002 REPEAT.ENTER count=4 -> exit=5' "$TMP/u32-repeat-add-qir.out"
grep -q '0003 U32.SET.ADD' "$TMP/u32-repeat-add-qir.out"
grep -q '0004 REPEAT.NEXT  -> enter=2' "$TMP/u32-repeat-add-qir.out"

"$BIN" check examples/u32_repeat_add.qn > "$TMP/u32-repeat-add-check.out"
grep -q '^PASS: QBIT_NOVA_NATIVE_CHECK_V01$' "$TMP/u32-repeat-add-check.out"

"$BIN" build examples/u32_repeat_add.qn -o "$TMP/u32-repeat-add-a.qbc" >/dev/null
"$BIN" build examples/u32_repeat_add.qn -o "$TMP/u32-repeat-add-b.qbc" >/dev/null
cmp "$TMP/u32-repeat-add-a.qbc" "$TMP/u32-repeat-add-b.qbc"

python3 - "$TMP/u32-repeat-add-a.qbc" <<'PY'
import struct
import sys

path = sys.argv[1]
data = open(path, 'rb').read()
assert data[:4] == b'QBCN'
version = struct.unpack_from('<H', data, 4)[0]
header_size = struct.unpack_from('<H', data, 6)[0]
insn_count = struct.unpack_from('<I', data, 8)[0]
scalar_count = struct.unpack_from('<H', data, 76)[0]
bool_mask = struct.unpack_from('<Q', data, 80)[0]
assert version == 7
assert header_size == 88
assert insn_count == 7
assert scalar_count == 2
assert bool_mask == 0
ops = []
for i in range(insn_count):
    off = header_size + i * 8
    op, a, b, flags, imm = struct.unpack_from('<BBBBI', data, off)
    ops.append((op, a, b, flags, imm))
assert ops[2] == (0x64, 4, 0, 0, 5)
assert ops[3] == (0x60, 0, 0, 1, 0)
assert ops[4] == (0x65, 0, 0, 0, 2)
assert ops[5][0] == 0x53
assert ops[6] == (0x7f, 0, 0, 0, 0)
print('QBC_V7_BOUNDED_REPEAT_LAYOUT=PASS')
PY

"$BIN" run examples/u32_repeat_add.qn --backend cpu \
  --receipt "$TMP/u32-repeat-add.json" \
  > "$TMP/u32-repeat-add.out"
grep -q '^QBIT_NOVA_NATIVE_BOUNDED_REPEAT_RUN_V07_STEP5$' \
  "$TMP/u32-repeat-add.out"
grep -q '^boundary=native_typed_u32_bounded_repeat$' \
  "$TMP/u32-repeat-add.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/u32-repeat-add.out"
grep -q '^qvm_operation=bounded-repeat-program$' "$TMP/u32-repeat-add.out"
grep -q '^scalar_contract=typed-u32-bounded-repeat-v1$' \
  "$TMP/u32-repeat-add.out"
grep -q '^control_flow=bounded-repeat-only$' "$TMP/u32-repeat-add.out"
grep -q '^general_backward_jump=false$' "$TMP/u32-repeat-add.out"
grep -q '^repeat_iterations=4$' "$TMP/u32-repeat-add.out"
grep -q '^repeat_max_iterations=1024$' "$TMP/u32-repeat-add.out"
grep -q '^execution_step_bound=16$' "$TMP/u32-repeat-add.out"
grep -q '^execution_step_limit=1000000$' "$TMP/u32-repeat-add.out"
grep -q '^emitted_u32=4$' "$TMP/u32-repeat-add.out"
grep -q '"marker": "QBIT_NOVA_NATIVE_BOUNDED_REPEAT_RECEIPT_V07_STEP5"' \
  "$TMP/u32-repeat-add.json"
grep -q '"control_flow": "bounded-repeat-only"' "$TMP/u32-repeat-add.json"
grep -q '"repeat_iterations": 4' "$TMP/u32-repeat-add.json"
grep -q '"execution_step_bound": 16' "$TMP/u32-repeat-add.json"

"$BIN" run examples/u32_repeat_add.qn --backend auto \
  > "$TMP/u32-repeat-auto.out"
grep -q '^qvm_selected_backend=cpu$' "$TMP/u32-repeat-auto.out"
grep -q '^qvm_selection_reason=scalar-operation-not-gpu-eligible$' \
  "$TMP/u32-repeat-auto.out"
grep -q '^qvm_operation=bounded-repeat-program$' "$TMP/u32-repeat-auto.out"
grep -q '^qvm_gpu_execution_attempted=false$' "$TMP/u32-repeat-auto.out"
grep -q '^qvm_cpu_fallback=true$' "$TMP/u32-repeat-auto.out"
grep -q '^emitted_u32=4$' "$TMP/u32-repeat-auto.out"

set +e
"$BIN" run examples/u32_repeat_add.qn --backend vulkan \
  >"$TMP/u32-repeat-vulkan.out" 2>"$TMP/u32-repeat-vulkan.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7201' "$TMP/u32-repeat-vulkan.err"
test ! -s "$TMP/u32-repeat-vulkan.out"

for SPEC in \
  'examples/u32_repeat_1.qn 8' \
  'examples/u32_repeat_add.qn 4' \
  'examples/u32_repeat_sub.qn 6' \
  'examples/u32_repeat_mul.qn 54' \
  'examples/u32_repeat_div.qn 4' \
  'examples/u32_repeat_multiple.qn 4' \
  'examples/u32_repeat_1024.qn 1024'
do
  set -- $SPEC
  SOURCE="$1"
  EXPECTED="$2"
  NAME="$(basename "$SOURCE" .qn)"
  "$BIN" run "$SOURCE" --backend cpu \
    > "$TMP/${NAME}.out"
  grep -q "^emitted_u32=${EXPECTED}$" "$TMP/${NAME}.out"
done

set +e
"$BIN" run tests/bad_repeat_div_zero.qn --backend cpu \
  >"$TMP/bad-repeat-div-zero.out" 2>"$TMP/bad-repeat-div-zero.err"
STATUS=$?
set -e
test "$STATUS" -eq 7
grep -q 'QN-E7517' "$TMP/bad-repeat-div-zero.err"
test ! -s "$TMP/bad-repeat-div-zero.out"

for SPEC in \
  'bad_repeat_zero QN-E7550' \
  'bad_repeat_1025 QN-E7550' \
  'bad_repeat_missing_brace QN-E7552' \
  'bad_repeat_empty QN-E7553' \
  'bad_repeat_nested QN-E7554' \
  'bad_repeat_if_inside QN-E7555' \
  'bad_repeat_inside_if QN-E7555' \
  'bad_repeat_emit_inside QN-E7557' \
  'bad_repeat_let_inside QN-E7556' \
  'bad_set_outside_repeat QN-E7558' \
  'bad_repeat_unknown_target QN-E7560' \
  'bad_repeat_unknown_operand QN-E7561' \
  'bad_repeat_bool_target QN-E7562' \
  'bad_repeat_bool_operand QN-E7562' \
  'bad_repeat_comparison_inside QN-E7559' \
  'bad_repeat_missing_final_emit QN-E7563' \
  'bad_repeat_two_blocks QN-E7551'
do
  set -- $SPEC
  CASE="$1"
  CODE="$2"
  set +e
  "$BIN" check "tests/${CASE}.qn" \
    >"$TMP/${CASE}.out" 2>"$TMP/${CASE}.err"
  STATUS=$?
  set -e
  test "$STATUS" -ne 0
  test ! -s "$TMP/${CASE}.out"
  grep -q "$CODE" "$TMP/${CASE}.err"
done

# Deterministic static execution-budget rejection: 1024 iterations over
# 1000 mutations exceeds the one-million-step VM ceiling before execution.
{
  echo 'let total: u32 = 0'
  echo 'let one: u32 = 1'
  echo 'repeat 1024 {'
  for _ in $(seq 1 1000); do
    echo '    set total = total + one'
  done
  echo '}'
  echo 'emit total'
} > "$TMP/bad-repeat-budget.qn"
set +e
"$BIN" check "$TMP/bad-repeat-budget.qn" \
  >"$TMP/bad-repeat-budget.out" 2>"$TMP/bad-repeat-budget.err"
STATUS=$?
set -e
test "$STATUS" -eq 8
test ! -s "$TMP/bad-repeat-budget.out"
grep -q 'QN-E7565' "$TMP/bad-repeat-budget.err"

# QBC v7 may contain the dedicated repeat back-edge only. A version downgrade
# or malformed repeat pairing must fail closed during decode/contract checks.
cp "$TMP/u32-repeat-add-a.qbc" "$TMP/u32-repeat-v6-tamper.qbc"
python3 - "$TMP/u32-repeat-v6-tamper.qbc" <<'PY'
import struct
import sys
with open(sys.argv[1], 'r+b') as f:
    f.seek(4)
    f.write(struct.pack('<H', 6))
PY
set +e
"$BIN" exec "$TMP/u32-repeat-v6-tamper.qbc" --backend cpu \
  >"$TMP/u32-repeat-v6-tamper.out" 2>"$TMP/u32-repeat-v6-tamper.err"
STATUS=$?
set -e
test "$STATUS" -eq 6
grep -q 'QN-E7567' "$TMP/u32-repeat-v6-tamper.err"
test ! -s "$TMP/u32-repeat-v6-tamper.out"

cp "$TMP/u32-repeat-add-a.qbc" "$TMP/u32-repeat-target-tamper.qbc"
python3 - "$TMP/u32-repeat-target-tamper.qbc" <<'PY'
import struct
import sys
with open(sys.argv[1], 'r+b') as f:
    # v7 header 88 + REPEAT_NEXT instruction index 4 * 8 + imm offset 4
    f.seek(88 + 4 * 8 + 4)
    f.write(struct.pack('<I', 1))
PY
set +e
"$BIN" exec "$TMP/u32-repeat-target-tamper.qbc" --backend cpu \
  >"$TMP/u32-repeat-target-tamper.out" 2>"$TMP/u32-repeat-target-tamper.err"
STATUS=$?
set -e
test "$STATUS" -eq 6
grep -Eq 'QN-E7509|QN-E7569' "$TMP/u32-repeat-target-tamper.err"
test ! -s "$TMP/u32-repeat-target-tamper.out"

# Step 1/2/3/4 and Stage 6 bytecode are frozen. Step 5 must not alter them.
for SPEC in \
  'examples/u32_scalar.qn 9c7c0638cf508ad5c690f9764395a40679f183ef4d6f5681fb101cb0e025aa12' \
  'examples/u32_scalar_sub.qn fa5174d59fce3a500a841472eb724216bf22b8c9cb30a604e7600c9e0d78e6d6' \
  'examples/u32_scalar_mul.qn e1d0d2f4814dfbb914eaba3ed1330965a4b11bc2d76f6d4dbfe3af051e4079d8' \
  'examples/u32_scalar_div.qn 3bec38d21273192423967cc11520c4206e1409b245a37bd11c31cf0514af14d9' \
  'examples/u32_compare_eq.qn 00ee1eca5e1a469cf14446b571f4b0287dea322b799b0a747994383469397014' \
  'examples/u32_compare_ne.qn 2640ce131b1bf743665c69e319f5d83ecdb9467510fa99d66f2a4912b65e12fc' \
  'examples/u32_compare_lt.qn 8d5b31294afb0c6970e53e2cb6069d7edb9f5454ea94e0f968a4d88a32542293' \
  'examples/u32_compare_le.qn 053044f80ed3925bdca779e3eb4b6e8fea7cd6b65112b71849e2445570147d3b' \
  'examples/u32_compare_gt.qn 3b11c46a881e5b1bc0e37570e450ba898769f1d1782a7619c2440785eee34576' \
  'examples/u32_compare_ge.qn c25981d35b6ebc7d5ea281c6fc1cb527ebe9ed256bd41ff5c0c7d1c0d85d04aa' \
  'examples/u32_if_else_true.qn 4032917c63e8efeb767791a73efbdd10aba525be23606540ee585305e05811cd' \
  'examples/u32_if_else_false.qn 9733d164c22dbbb6db2e5cfaf76c6ebb6b27c4f4568fd0295ef378790352cbad' \
  'examples/u32_if_else_branch_local.qn 68ebccf826db0e3dcec8274318f53ae201b7f40af6e823eb2ab788175169cc5f' \
  'examples/vector_add_u32.qn 9f9a624a88200847595c3a5499dc03fe4811bef1676e988fac4ce53a8448642f'
do
  set -- $SPEC
  SOURCE="$1"
  EXPECTED_SHA="$2"
  OUT="$TMP/step5-regression-$(basename "$SOURCE" .qn).qbc"
  "$BIN" build "$SOURCE" -o "$OUT" >/dev/null
  test "$(sha256sum "$OUT" | awk '{print $1}')" = "$EXPECTED_SHA"
done

echo "BOUNDED_REPEAT_PIPELINE=PASS"

echo "=== DETERMINISTIC QBC ==="
"$BIN" build examples/approval_model.qn -o "$TMP/a.qbc" >/dev/null
"$BIN" build examples/approval_model.qn -o "$TMP/b.qbc" >/dev/null
cmp "$TMP/a.qbc" "$TMP/b.qbc"

echo "PASS: QBIT_NOVA_IF_ELSE_CONTROL_FLOW_V07_STEP4"
echo "PASS: QBIT_NOVA_U32_COMPARISONS_BOOL_V07_STEP3"
echo "PASS: QBIT_NOVA_U32_ARITHMETIC_V07_STEP2"
echo "PASS: QBIT_NOVA_TYPED_U32_SCALAR_V07_STEP1"
echo "PASS: QBIT_NOVA_BOUNDED_GPU_OPERATION_V06_STEP5"
echo "PASS: QBIT_NOVA_QVM_GPU_ROUTING_V06_STEP4"
echo "PASS: QBIT_NOVA_REAL_V3D_COMPUTE_V06_STEP3"
echo "PASS: QBIT_NOVA_GPU_ADAPTER_CONTRACT_V06_STEP2"
echo "PASS: QBIT_NOVA_RECEIPT_EVIDENCE_V052_STEP8"
echo "PASS: QBIT_NOVA_REVOCATION_EXECUTION_WIRING_V052_STEP7"
echo "PASS: QBIT_NOVA_REPLAY_EXECUTION_BOUNDARY_V052_STEP5"
echo "PASS: QBIT_NOVA_TRUSTED_ISSUER_CLI_V052_STEP3"
echo "PASS: QBIT_NOVA_OPENSSL_ED25519_APPROVAL_TEST_SUITE_V05"
