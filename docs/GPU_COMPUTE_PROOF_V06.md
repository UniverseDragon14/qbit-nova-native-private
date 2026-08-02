# QBIT NOVA Real V3D Compute Proof v0.6

Stage 6 Step 3 adds one bounded, deterministic Vulkan compute proof for
Raspberry Pi 5 V3D hardware.

## Fixed operation

The embedded SPIR-V compute shader performs exactly 256 unsigned 32-bit
vector additions. It uses:

- one 3072-byte host-visible coherent storage buffer
- 64 invocations per workgroup
- four workgroups
- one storage-buffer descriptor
- one compute queue
- no network, shell, arbitrary file, int64, or float64 operation

The input vectors and CPU reference are deterministic.

## Hardware boundary

Vulkan execution is allowed only when all of these conditions hold:

1. `/dev/dri/renderD128` is readable and writable
2. the Vulkan loader and required symbols are available
3. vendor ID is Broadcom `0x14e4`
4. device type is integrated GPU
5. device name starts with `V3D `
6. the device is not llvmpipe
7. a compute-capable queue exists
8. host-visible coherent storage memory exists

Explicit Vulkan requests fail closed when any condition is not met.

## Validation boundary

After queue completion, every GPU-produced uint32 value is compared with the
deterministic CPU reference. A mismatch fails with `QN-E7118` and does not
produce a successful receipt.

## CLI

    qnova gpu compute-proof --backend cpu
    qnova gpu compute-proof --backend vulkan --receipt proof.json
    qnova gpu compute-proof --backend auto

`auto` uses CPU only when the verified V3D hardware candidate is unavailable.
Once a valid candidate exists, a Vulkan execution failure is not silently
downgraded to CPU.

## Receipt

Successful output and JSON record:

- selected backend and reason
- hardware identity
- fixed element count, local size, and dispatch count
- GPU attempted/completed state
- CPU-reference validation
- result match
- embedded shader SHA-256
- output SHA-256

This is a bounded compute proof, not general arbitrary shader execution and
not yet integration of the quantum-state simulation with the GPU backend.
