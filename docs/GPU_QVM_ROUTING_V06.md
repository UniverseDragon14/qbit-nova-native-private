# QBIT NOVA QVM GPU Routing v0.6

Stage 6 Step 4 connects backend selection to `qnova run` and
`qnova exec` without claiming unsupported quantum GPU execution.

## CLI

Both source and QBC execution accept:

    --backend cpu|auto|vulkan

The default is `cpu`.

## Routing rules

The current QVM operation is identified as
`quantum-state-simulation`. The bounded Stage 6 Step 3 Vulkan kernel
implements only `uint32-vector-add`; it is not a quantum-state
simulator and is never substituted for QVM execution.

Routing is therefore:

- default CPU: execute on the existing C17 QVM
- explicit CPU: execute on the existing C17 QVM
- auto: deterministic CPU fallback with
  `qvm-operation-not-gpu-eligible`
- explicit Vulkan: fail closed with `QN-E7201`

Explicit Vulkan rejection happens after guard, approval, revocation,
and execution preflight checks, but before atomic replay consumption
and before QVM execution.

## Receipt evidence

Successful text output and JSON receipts include:

- requested and selected backend
- selection reason
- QVM operation
- GPU eligibility
- GPU execution attempted/completed state
- CPU fallback state

A successful QVM receipt never claims GPU execution in this step.

## Truth boundary

This step does not add:

- quantum state-vector GPU acceleration
- arbitrary SPIR-V or shader execution
- a general GPU runtime
- network, shell, or arbitrary file access
- silent fallback after an explicit Vulkan request

The verified V3D vector-add proof remains available only through
`qnova gpu compute-proof`.
