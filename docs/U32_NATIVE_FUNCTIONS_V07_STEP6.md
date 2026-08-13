# Stage 7 Step 6: Native Typed Functions

Stage 7 Step 6 adds bounded, typed `u32` functions to QBIT NOVA Native. The implementation uses real QVM call frames. Function bodies are not source-inlined and are not delegated to a host-language function runtime.

## Source contract

```qn
fn add(a: u32, b: u32) -> u32 {
    sum = a + b
    return sum
}

let left: u32 = 10
let right: u32 = 20
call add(left, right) -> total
emit total
```

Step 6 supports at most 16 top-level functions, at most 2 `u32` parameters per function, `u32` returns, local scalar frames, forward calls, nested acyclic calls, and a maximum active function-call depth of 8. Parameters are copied by value. Main and function-local scalar slots are isolated.

Function bodies may contain `let` literals, `u32` arithmetic, function calls, and one terminal `return`. `emit`, `if`, `repeat`, `set`, comparisons, bool values, quantum operations, vector operations, capabilities, nested functions, and recursion are rejected in this step. Main must contain at least one function call and exactly one final scalar emit.

## QBC v8 ABI

Function programs use QBC v8 with a 96-byte header. Bytes 0..87 preserve the prior header layout. The v8 extension is:

- offset 88: `function_count` (`u16`)
- offset 90: `function_record_size` (`u16`, exactly 12)
- offset 92: `main_entry_pc` (`u32`)

The function table follows the register table and precedes instructions. Each 12-byte record contains `entry_pc` (`u32`), exclusive `end_pc` (`u32`), local `scalar_count` (`u16`), `param_count` (`u8`), and zero `flags` (`u8`). The decoder additionally canonicalizes function ranges as contiguous, source-ordered pre-main regions. This rejects hidden/dead instruction gaps before main and makes the bytecode layout deterministic.

New opcodes:

- `0x66 OP_CALL`
- `0x67 OP_RETURN`
- `0x7f OP_END` remains unchanged

`CALL` stores the caller destination in `a`, argument 0 in `b`, argument 1 in `flags`, and the zero-based function-table index in `imm`. Parameter count comes from function metadata. `RETURN` stores the returned local scalar slot in `a`; all other fields are zero.

## QVM frame model

Each active function owns a fresh frame containing local values, initialization state, local scalar count, return PC, return destination, and function identity. A call copies arguments into the new frame, then jumps to the function entry. Return captures the initialized local result, pops the frame, writes into the caller destination, and resumes at the saved PC.

The compiler rejects direct and indirect recursion and statically validates call depth. The QBC validator repeats those checks defensively. Function execution is also bounded by an expanded acyclic call-graph step cost and the existing `QN_MAX_EXECUTION_STEPS=1000000` runtime ceiling.

## Routing and compatibility

Typed function programs are CPU-only in Step 6. `auto` selects CPU; explicit CPU is allowed; explicit Vulkan fails closed. Stage 6 bounded V3D vector compute remains independent.

QBC v1-v7 encoding remains unchanged. Frozen Stage 6 and Stage 7 Step 1 through Step 5 bytecode hashes are regression-tested exactly.
