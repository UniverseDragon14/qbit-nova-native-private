# QBIT NOVA Native Stage 7 Step 4: `if / else`

Stage 7 Step 4 adds bounded native scalar decision control flow without enabling loops, shell execution, network access, filesystem access, or device privilege.

## Source contract

```qn
let a: u32 = 10
let b: u32 = 20
less = a < b

if less {
    emit a
} else {
    emit b
}
```

The condition must be a native `bool`. Both branches are required, both must terminate with exactly one `emit`, and both emitted values must have the same type. Nested `if` is intentionally deferred.

## QIR / QBC contract

Control-flow programs lower to forward-only `JUMP.IF.FALSE` and `JUMP` instructions. QBC v6 keeps the 88-byte typed-scalar header and the `scalar_bool_mask` at offset 80.

- `0x5e`: `OP_JUMP_IF_FALSE`
- `0x5f`: `OP_JUMP`
- backward jumps are rejected
- out-of-range jumps are rejected
- scalar control flow remains CPU-only
- existing QBC v4/v5 programs remain byte-for-byte compatible

## Runtime contract

The typed scalar VM uses an explicit program counter. Stage 7 Step 4 permits only forward jumps, so execution is statically bounded by the instruction count. Exactly one branch reaches `END` with one emitted value.

## Compatibility and hardware boundary

Existing Stage 6 V3D vector compute remains unchanged. Scalar control flow is not GPU eligible and explicit Vulkan routing fails closed. Full Pi5 regression, replay-ledger execution, and real Broadcom V3D tests remain mandatory before freeze.
