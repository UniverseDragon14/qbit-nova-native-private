# QBIT NOVA Native Stage 7 Step 5: Bounded Repeat

Stage 7 Step 5 adds explicit, bounded mutation to the native typed scalar
runtime without weakening the immutable declaration model introduced in the
previous scalar stages.

## Syntax

```qn
let total: u32 = 0
let one: u32 = 1

repeat 4 {
    set total = total + one
}

emit total
```

`repeat` accepts a literal iteration count from 1 through 1024. `set` is legal
only inside the repeat body, targets an already initialized `u32` scalar, and
supports `+`, `-`, `*`, and `/`. The body cannot declare values, emit, branch,
or contain another repeat in Step 5.

## QBC v7

Bounded-repeat programs use the existing 88-byte typed-scalar header and add:

- `0x60 OP_U32_SET_ADD`
- `0x61 OP_U32_SET_SUB`
- `0x62 OP_U32_SET_MUL`
- `0x63 OP_U32_SET_DIV`
- `0x64 OP_REPEAT_ENTER`
- `0x65 OP_REPEAT_NEXT`

`OP_JUMP` and `OP_JUMP_IF_FALSE` remain forward-only. The only structurally
permitted backward relationship is `OP_REPEAT_NEXT` pointing to its matching
`OP_REPEAT_ENTER`.

## Bounded execution

The runtime accepts at most 1024 repeat iterations and at most 1,000,000
worst-case VM instruction steps. The QBC validator computes the bound before
execution. A repeat that exceeds the execution budget is rejected.

## Compatibility

Programs without Step 5 opcodes keep their previous QBC versions and byte
layout. The verified Stage 6 V3D vector-add path remains separate and
unchanged. Bounded scalar repeat is CPU-only and an explicit Vulkan request
fails closed.
