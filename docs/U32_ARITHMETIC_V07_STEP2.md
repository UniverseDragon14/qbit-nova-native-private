# QBIT NOVA `u32` Arithmetic Extension (Stage 7 Step 2)

This milestone extends the native typed `u32` scalar foundation from Stage 7
Step 1 with subtraction, multiplication, and division. It does not introduce a
host-language wrapper: `.qn` source continues through the QBIT NOVA lexer,
parser, typed QIR, QBC bytecode, and QVM.

## Source forms

```qn
let a: u32 = 30
let b: u32 = 12
difference = a - b
emit difference
```

```qn
let a: u32 = 6
let b: u32 = 7
product = a * b
emit product
```

```qn
let a: u32 = 100
let b: u32 = 4
quotient = a / b
emit quotient
```

Each arithmetic assignment creates a new initialized `u32`. Operands must refer
to previously initialized `u32` variables. Existing duplicate-variable,
unknown-variable, single-emit, CPU-routing, and deny-by-default rules remain in
force.

## Native pipeline additions

- Lexer tokens: `-`, `*`, `/`.
- AST statements: `U32_SUB`, `U32_MUL`, `U32_DIV`.
- Typed QIR: `U32.SUB`, `U32.MUL`, `U32.DIV`.
- QBC opcodes: `0x54`, `0x55`, `0x56`.
- QVM: bounded CPU execution using the existing 64-slot typed scalar store.
- Runtime division-by-zero diagnostic: `QN-E7517`.

## Arithmetic semantics

- `u32` width is exactly 32 bits.
- Addition, subtraction, and multiplication use modulo-2^32 arithmetic.
- Subtraction underflow wraps modulo 2^32.
- Multiplication overflow wraps modulo 2^32.
- Division is unsigned integer division; any remainder is discarded.
- Division by zero fails closed before an output or receipt is emitted.
- No implicit type conversion exists.
- Scalar arithmetic remains non-GPU-eligible in this milestone.
- Explicit Vulkan requests fail closed before scalar execution.

## QBC compatibility decision

QBC remains version 4 because the binary header, instruction width, scalar
metadata, and validation layout are unchanged. Stage 7 Step 2 extends the
reserved version-4 typed-scalar opcode family with `0x54` through `0x56`.
Existing Stage 7 Step 1 QBC and Stage 6 vector QBC remain byte-for-byte
unchanged and are regression-tested.

A future QBC version increment is required only when the binary layout or
incompatible decoding contract changes, not merely when a reserved opcode is
added compatibly.

## Safety boundary

This feature adds no shell, network, file-operation, model-execution,
device-control, arbitrary shader, or physical-QPU capability. It does not skip
security or replay-ledger tests. The complete existing test suite remains part
of the acceptance boundary.
