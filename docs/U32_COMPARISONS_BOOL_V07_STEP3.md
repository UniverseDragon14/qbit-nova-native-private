# U32 Comparisons and Native Bool — Stage 7 Step 3

## Scope

Stage 7 Step 3 adds six native unsigned-32 comparison operators to QBIT NOVA:

```qn
left == right
left != right
left < right
left <= right
left > right
left >= right
```

The operands are typed `u32`. The result is a native QBIT NOVA `bool`, not an implicitly interchangeable `u32`. Bool values are represented internally as canonical `0` or `1`, while type metadata remains explicit through typed QIR and QBC.

## Native pipeline

```text
QBIT NOVA source
  -> native lexer
  -> native parser / AST
  -> semantic type validation
  -> typed QIR
  -> QBC
  -> QVM
  -> deterministic evidence receipt
```

No Python, JavaScript, Rust, shell, or other programming language runtime is used to interpret QBIT NOVA programs. The implementation runtime is native C17.

## Example

```qn
let a: u32 = 10
let b: u32 = 20
less = a < b
emit less
```

Typed result:

```text
u32 < u32 -> bool
```

Arithmetic remains strictly typed:

```text
u32 + u32 -> u32
bool + u32 -> rejected
```

Comparison operands must both be `u32`; comparing a `bool` as though it were a `u32` is rejected. There is no implicit scalar type conversion.

## QBC compatibility

Pure Stage 7 Step 1/2 scalar programs remain QBC version 4 and preserve their byte-for-byte encoding. Programs containing native bool slots use QBC version 5.

QBC v5 keeps the existing instruction width and extends the header from 80 to 88 bytes:

```text
offset 76 : scalar_count       (u16)
offset 78 : reserved           (u16, zero)
offset 80 : scalar_bool_mask   (u64)
```

`scalar_bool_mask` has one bit per scalar slot. A set bit marks a native bool slot. With the current bounded 64-slot scalar model, one deterministic 64-bit mask covers the complete scalar type map.

Comparison opcodes:

```text
0x57 U32_EQ
0x58 U32_NE
0x59 U32_LT
0x5a U32_LE
0x5b U32_GT
0x5c U32_GE
0x5d BOOL_EMIT
```

Comparison and bool opcodes are fail-closed outside QBC v5. The decoder validates slot bounds, scalar types, initialization order, one-result emission, and the final `OP_END` contract.

## Bool evidence

A bool output is hashed from exactly one canonical byte:

```text
false -> 0x00
true  -> 0x01
```

This is deliberately distinct from the existing 4-byte little-endian `u32` output digest contract. Old u32 receipts and output digests remain unchanged.

## Execution boundary

Typed scalar comparison execution is CPU-only in Step 3. Explicit Vulkan requests fail closed. Auto routing selects CPU and records the non-GPU-eligible reason in the evidence output.

## Future dependency

Native bool is the type-system foundation for Stage 7 conditional control flow. `if/else` is intentionally not implemented in this step.
