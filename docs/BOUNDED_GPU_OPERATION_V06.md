# Native Bounded GPU Operation v0.6

Stage 6 Step 5 adds the first GPU-eligible operation that travels through the
native QBIT NOVA language pipeline instead of using the standalone GPU proof
command.

## Source contract

```qn
vector_add_u32 -> sum
emit sum
```

`u32_vector_add` is accepted as an equivalent keyword. The operation has no
user-supplied buffers in this step. It always uses the deterministic fixed
input contract already verified by the Stage 6 Step 3 V3D proof.

The operation is deliberately bounded:

- exactly 256 unsigned 32-bit elements;
- unsigned 32-bit modulo arithmetic;
- one vector-add operation and one emitted result;
- no quantum statements in the same program;
- no `shots` or `seed`, including explicit zero values;
- no arbitrary shader, buffer length, file, shell, or network input.

## Native pipeline

The source is represented throughout the native pipeline:

```text
TOK_VECTOR_ADD_U32
  -> STMT_VECTOR_ADD_U32
  -> QIR_OP_U32_VECTOR_ADD : u32vec<256>
  -> OP_U32_VECTOR_ADD (0x50, imm=256)
  -> routed QVM execution
  -> CPU or verified Broadcom V3D backend
  -> CPU-reference validation
  -> deterministic evidence receipt
```

The exact compute QBC contract is:

- QBC v3;
- zero qubits and zero quantum registers;
- capabilities `compute.u32_vector_add,evidence.emit` only;
- three instructions in this order:
  1. `OP_U32_VECTOR_ADD`, immediate value 256;
  2. `OP_EMIT`;
  3. `OP_END`.

Malformed, mixed, or tampered compute bytecode is rejected before execution.

## Routing policy

| Request | Hardware state | Result |
|---|---|---|
| default | any | CPU |
| `--backend cpu` | any | CPU |
| `--backend auto` | verified V3D available | Vulkan V3D |
| `--backend auto` | supported hardware unavailable | deterministic CPU fallback |
| `--backend vulkan` | verified V3D available | Vulkan V3D |
| `--backend vulkan` | supported hardware unavailable | fail closed |

An auto route may fall back only when supported hardware is unavailable at the
routing boundary. A Vulkan execution error, malformed evidence, or CPU/GPU
result mismatch fails closed rather than silently changing the claimed result.

Quantum-state simulation remains CPU-only. The fixed vector-add shader is not
and must not be represented as a quantum kernel.

## Execution evidence

Successful native compute output uses:

```text
QBIT_NOVA_NATIVE_COMPUTE_RUN_V06
boundary=native_bounded_compute
qvm_operation=bounded-uint32-vector-add
```

The JSON receipt marker is:

```text
QBIT_NOVA_NATIVE_COMPUTE_RECEIPT_V06
```

The output and receipt record at least:

- requested and selected backend;
- selection reason and CPU fallback state;
- GPU eligibility and actual attempted/completed state;
- element count and arithmetic semantics;
- hardware device and vendor identifier;
- CPU-reference validation and result match;
- embedded shader SHA-256;
- deterministic output SHA-256;
- source and QBC SHA-256;
- capability, approval, revocation, and replay evidence.

The expected fixed output digest remains:

```text
68a036d2c3ab163b9ff7ac887010b699fba71e94c89c29f4da908e08a9e5026b
```

## Security boundary

Routing and compute preflight occur before replay-ledger consumption. Invalid
backend requests, illegal `shots`/`seed` options, malformed QBC, unavailable
explicit Vulkan hardware, and evidence mismatch therefore fail before a
successful execution receipt is emitted.

This step does not add a general GPU runtime. It enables one fixed native
operation backed by one embedded, reviewed SPIR-V kernel.
