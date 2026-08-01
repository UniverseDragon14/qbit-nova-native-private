# QBIT NOVA GPU Adapter Contract v0.6

Stage 6 Step 2 establishes a native, deny-by-default GPU adapter contract.
It does not claim GPU execution and does not add a GPU compute kernel.

## Verified Pi 5 profile

The source capture records the following Raspberry Pi 5 evidence:

- hardware device: `V3D 7.1.7.0`
- driver: `V3DV Mesa 25.0.7`
- render node: `/dev/dri/renderD128`
- Vulkan compute queue: available
- maximum contract workgroup invocations: 256
- maximum contract compute shared memory: 16384 bytes
- contract subgroup size: 16
- 64-bit integer shaders: disabled
- 64-bit floating-point shaders: disabled
- OpenCL: unavailable and not required

These values are conservative adapter-profile limits, not permission to
execute an arbitrary shader.

## Strict hardware candidate

A Vulkan device is a supported hardware candidate only when all conditions
hold:

1. vendor ID is Broadcom `0x14e4`
2. device type is integrated GPU
3. device name starts with `V3D `
4. at least one queue supports compute
5. the current process has read/write access to `/dev/dri/renderD128`
6. the device is not `llvmpipe`

`llvmpipe` is always classified as CPU software rendering. It cannot produce
a hardware-GPU claim.

## Selection boundary

`qnova gpu probe --backend auto` performs read-only discovery and records a
CPU fallback decision. Even when a valid V3D candidate exists, Stage 6 Step 2
selects the existing CPU backend with reason `gpu-kernel-not-implemented`.

`qnova gpu probe --backend vulkan` fails closed with `QN-E7005`. A later stage
must add and verify an actual bounded compute pipeline before Vulkan execution
can be enabled.

## Runtime dependency

The probe dynamically loads `libvulkan.so.1`. Vulkan headers and
`libvulkan-dev` are not required for this contract-only step. No package is
installed by the patch.

## Receipt

The optional JSON receipt records:

- requested and selected backend
- selection reason
- Vulkan loader availability
- render-node access
- strict hardware-candidate availability
- device identity when accepted
- compute-queue status
- llvmpipe observation and rejection
- GPU execution disabled state
- CPU fallback state
- conservative GPU profile limits

The receipt is a backend-decision receipt, not proof that a GPU kernel ran.
