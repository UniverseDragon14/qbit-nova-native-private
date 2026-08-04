# QBIT NOVA Typed `u32` Scalar Foundation (Stage 7 Step 1)

This milestone adds the first general typed scalar variables to the native
QBIT NOVA language pipeline.

## Source forms

```qn
let a: u32 = 10
let b: u32 = 20
sum = a + b
emit sum
```

`let` declares and initializes a new `u32`. An addition assignment creates a
new `u32` from two previously initialized `u32` variables. `emit` selects one
initialized variable as the deterministic public result.

## Typed pipeline

- Lexer tokens: `let`, `u32`, `:`, `=`, `+`.
- AST statements: `U32_LET`, `U32_ADD`.
- Typed QIR: `U32.CONST`, `U32.ADD`, `U32.EMIT`.
- QBC version 4 opcodes: `0x51`, `0x52`, `0x53`.
- QVM execution: CPU-only, bounded to 64 scalar slots.
- Receipt: `QBIT_NOVA_NATIVE_SCALAR_RECEIPT_V07`.

## Semantics

- Integer width is exactly 32 bits.
- Addition uses unsigned modulo-2^32 arithmetic.
- No implicit type conversion exists.
- Duplicate variables are rejected.
- Unknown or not-yet-initialized operands are rejected.
- Exactly one initialized scalar must be emitted.
- `--shots` and `--seed` are rejected.
- Scalar execution is not GPU-eligible in this milestone.
- Explicit Vulkan requests fail closed before execution.

## Safety boundary

This feature introduces no shell, network, file-operation, model-execution,
device-control, arbitrary shader, or physical-QPU capability. Existing Stage 6
bounded V3D vector execution remains unchanged and separately verified.
