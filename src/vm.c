#include "qn_vm.h"
#include "qn_gpu_compute.h"

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t s; } RNG;

static uint64_t rng_next(RNG *r) {
    uint64_t x = r->s ? r->s : UINT64_C(0x9e3779b97f4a7c15);
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return x * UINT64_C(2685821657736338717);
}

static double rng_unit(RNG *r) {
    return (rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

static void gate_x(double complex *amp, size_t dim, unsigned q) {
    size_t mask = (size_t)1 << q;
    for (size_t i=0;i<dim;i++) if (!(i&mask)) {
        double complex t=amp[i]; amp[i]=amp[i|mask]; amp[i|mask]=t;
    }
}

static void gate_z(double complex *amp, size_t dim, unsigned q) {
    size_t mask=(size_t)1<<q;
    for(size_t i=0;i<dim;i++) if(i&mask) amp[i]=-amp[i];
}

static void gate_h(double complex *amp, size_t dim, unsigned q) {
    const double inv=0.70710678118654752440;
    size_t mask=(size_t)1<<q;
    for(size_t i=0;i<dim;i++) if(!(i&mask)) {
        double complex a=amp[i], b=amp[i|mask];
        amp[i]=(a+b)*inv;
        amp[i|mask]=(a-b)*inv;
    }
}

static void gate_cx(double complex *amp, size_t dim, unsigned c, unsigned t) {
    size_t cm=(size_t)1<<c, tm=(size_t)1<<t;
    for(size_t i=0;i<dim;i++) if((i&cm)&&!(i&tm)) {
        double complex x=amp[i]; amp[i]=amp[i|tm]; amp[i|tm]=x;
    }
}

static uint64_t measure_all(double complex *amp, size_t dim, RNG *rng) {
    double r=rng_unit(rng), acc=0.0;
    size_t chosen=dim-1;
    for(size_t i=0;i<dim;i++) {
        double re=creal(amp[i]), im=cimag(amp[i]);
        acc += re*re + im*im;
        if(r <= acc) { chosen=i; break; }
    }
    memset(amp,0,dim*sizeof(*amp));
    amp[chosen]=1.0+0.0*I;
    return chosen;
}

static int hist_add(QNRunResult *r, uint64_t state) {
    for(size_t i=0;i<r->entry_count;i++) {
        if(r->entries[i].state==state){ r->entries[i].count++; return 1; }
    }
    QNHistogramEntry *next=realloc(r->entries,(r->entry_count+1)*sizeof(*next));
    if(!next) return 0;
    r->entries=next;
    r->entries[r->entry_count]=(QNHistogramEntry){state,1};
    r->entry_count++;
    return 1;
}

void qn_run_result_free(QNRunResult *result) {
    if(!result) return;
    free(result->entries);
    memset(result,0,sizeof(*result));
}

static void qn_vm_copy_route(QNRunResult *out,
                             const QNGpuQvmRoute *route) {
    snprintf(
        out->qvm_requested_backend,
        sizeof(out->qvm_requested_backend),
        "%s",
        qn_gpu_backend_name(route->requested)
    );
    snprintf(
        out->qvm_selected_backend,
        sizeof(out->qvm_selected_backend),
        "%s",
        route->selected_backend
    );
    snprintf(
        out->qvm_selection_reason,
        sizeof(out->qvm_selection_reason),
        "%s",
        route->selection_reason
    );
    snprintf(
        out->qvm_operation,
        sizeof(out->qvm_operation),
        "%s",
        route->operation
    );
    out->qvm_gpu_eligible = route->gpu_eligible;
    out->qvm_gpu_execution_attempted =
        route->gpu_execution_attempted;
    out->qvm_gpu_execution_completed =
        route->gpu_execution_completed;
    out->qvm_cpu_fallback = route->cpu_fallback;
}

static QNStatus qn_vm_prepare_result(const QNBytecode *bc,
                                     const QNGuardPolicy *policy,
                                     const QNGpuQvmRoute *route,
                                     QNRunResult *out,
                                     QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));

    QNStatus guard_status =
        qn_guard_enforce(bc->capability_mask, policy, diag);
    if (guard_status != QN_OK) {
        return guard_status;
    }

    out->approved_capabilities =
        policy->approved & bc->capability_mask;
    out->has_approval_digest =
        policy->has_approval_digest;
    if (policy->has_approval_digest) {
        memcpy(
            out->approval_digest,
            policy->approval_digest,
            sizeof(out->approval_digest)
        );
    }
    snprintf(
        out->approval_scheme,
        sizeof(out->approval_scheme),
        "%s",
        policy->approval_scheme[0]
            ? policy->approval_scheme
            : "none"
    );
    out->has_approval_issuer =
        policy->has_approval_issuer;
    if (policy->has_approval_issuer) {
        memcpy(
            out->approval_issuer_fingerprint,
            policy->approval_issuer_fingerprint,
            sizeof(out->approval_issuer_fingerprint)
        );
    }

    qn_vm_copy_route(out, route);

    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    QNStatus status = qn_qbc_encode(
        bc,
        &encoded,
        &encoded_size,
        diag
    );
    if (status != QN_OK) {
        return status;
    }

    qn_sha256(encoded, encoded_size, out->qbc_digest);
    free(encoded);
    return QN_OK;
}

static QNStatus qn_vm_run_bounded_compute(
    const QNBytecode *bc,
    uint32_t shots,
    uint64_t seed,
    const QNGpuQvmRoute *route,
    QNRunResult *out,
    QNDiagnostic *diag
) {
    (void)bc;

    if (shots != 0u || seed != 0u) {
        qn_diag_set_code(
            diag,
            "QN-E7410",
            0,
            0,
            "bounded vector-add does not accept --shots or --seed"
        );
        return QN_ERR_PARSE;
    }

    QNGpuBackendRequest execution_backend;
    if (strcmp(route->selected_backend, "cpu") == 0) {
        execution_backend = QN_GPU_BACKEND_CPU;
    } else if (strcmp(route->selected_backend, "vulkan") == 0) {
        execution_backend = QN_GPU_BACKEND_VULKAN;
    } else {
        qn_diag_set_code(
            diag,
            "QN-E7411",
            0,
            0,
            "bounded vector-add route selected invalid backend '%s'",
            route->selected_backend
        );
        return QN_ERR_RUNTIME;
    }

    QNGpuComputeProof proof;
    QNStatus status = qn_gpu_compute_proof(
        execution_backend,
        &proof,
        diag
    );
    if (status != QN_OK) {
        return status;
    }

    if (!proof.selected_backend ||
        strcmp(proof.selected_backend, route->selected_backend) != 0 ||
        proof.element_count != QN_U32_VECTOR_ADD_COUNT ||
        !proof.cpu_reference_validated ||
        !proof.result_match) {
        qn_diag_set_code(
            diag,
            "QN-E7412",
            0,
            0,
            "bounded vector-add execution evidence mismatch"
        );
        return QN_ERR_RUNTIME;
    }

    out->native_compute_result = true;
    out->shots = 1u;
    out->seed = 0u;
    out->qvm_gpu_execution_attempted =
        proof.gpu_execution_attempted;
    out->qvm_gpu_execution_completed =
        proof.gpu_execution_completed;
    out->qvm_cpu_fallback = route->cpu_fallback;
    out->compute_element_count = proof.element_count;
    out->compute_cpu_reference_validated =
        proof.cpu_reference_validated;
    out->compute_result_match = proof.result_match;
    snprintf(
        out->compute_hardware_device,
        sizeof(out->compute_hardware_device),
        "%s",
        proof.hardware_device[0]
            ? proof.hardware_device
            : "none"
    );
    out->compute_hardware_vendor_id =
        proof.hardware_vendor_id;
    memcpy(
        out->compute_shader_digest,
        proof.shader_digest,
        sizeof(out->compute_shader_digest)
    );
    memcpy(
        out->compute_output_digest,
        proof.output_digest,
        sizeof(out->compute_output_digest)
    );

    return QN_OK;
}

typedef struct {
    uint32_t values[QN_MAX_SCALARS];
    bool initialized[QN_MAX_SCALARS];
    uint16_t scalar_count;
    size_t return_pc;
    uint8_t return_destination;
    uint16_t function_index;
} QNFunctionFrame;

static void qn_finish_u32_scalar_result(const QNBytecode *bc,
                                        const QNGpuQvmRoute *route,
                                        QNRunResult *out,
                                        uint16_t output_id,
                                        uint32_t output_value) {
    (void)bc;
    out->native_scalar_result = true;
    out->shots = 1u;
    out->seed = 0u;
    out->scalar_output_id = output_id;
    out->scalar_output_value = output_value;
    out->scalar_output_is_bool = false;
    out->qvm_gpu_execution_attempted = false;
    out->qvm_gpu_execution_completed = false;
    out->qvm_cpu_fallback = route->cpu_fallback;
    uint8_t encoded[4] = {
        (uint8_t)output_value,
        (uint8_t)(output_value >> 8),
        (uint8_t)(output_value >> 16),
        (uint8_t)(output_value >> 24)
    };
    qn_sha256(encoded, sizeof(encoded), out->scalar_output_digest);
}

static QNStatus qn_vm_run_function_scalar(const QNBytecode *bc,
                                           const QNGpuQvmRoute *route,
                                           const QNRuntimeInputs *runtime_inputs,
                                           QNRunResult *out,
                                           QNDiagnostic *diag) {
    uint64_t execution_bound = 0u;
    if (!qn_qbc_execution_step_bound(bc, &execution_bound)) {
        qn_diag_set_code(diag, "QN-E7593", 0, 0,
                         "function execution budget is invalid or exceeds %u",
                         QN_MAX_EXECUTION_STEPS);
        return QN_ERR_LIMIT;
    }

    QNFunctionFrame frames[QN_MAX_CALL_DEPTH + 1u];
    memset(frames, 0, sizeof(frames));
    frames[0].scalar_count = bc->scalar_count;
    frames[0].function_index = UINT16_MAX;

    if (qn_qbc_has_runtime_inputs(bc)) {
        if (!runtime_inputs || !runtime_inputs->provided ||
            runtime_inputs->count != bc->input_count) {
            qn_diag_set_code(diag, "QN-E7615", 0, 0,
                             "complete runtime input bindings are required");
            return QN_ERR_PARSE;
        }
        for (uint16_t i = 0u; i < bc->input_count; ++i) {
            uint16_t slot = bc->inputs[i].main_scalar_slot;
            if (slot >= frames[0].scalar_count ||
                frames[0].initialized[slot]) {
                qn_diag_set_code(diag, "QN-E7616", 0, 0,
                                 "invalid runtime input scalar slot");
                return QN_ERR_QBC;
            }
            frames[0].values[slot] = runtime_inputs->values[i];
            frames[0].initialized[slot] = true;
        }
        out->runtime_inputs_provided = true;
        out->runtime_input_abi = QN_RUNTIME_INPUT_ABI_V1;
        out->runtime_input_count = runtime_inputs->count;
        memcpy(out->runtime_input_digest,
               runtime_inputs->input_sha256,
               sizeof(out->runtime_input_digest));
    } else if (runtime_inputs && runtime_inputs->provided) {
        qn_diag_set_code(diag, "QN-E7617", 0, 0,
                         "runtime inputs are not accepted by this QBC program");
        return QN_ERR_PARSE;
    }

    uint8_t depth = 0u;
    size_t pc = bc->main_entry_pc;
    uint64_t executed = 0u;
    bool emitted = false;
    bool terminated = false;
    uint16_t emitted_id = 0u;
    uint32_t emitted_value = 0u;

    while (pc < bc->instruction_count) {
        if (++executed > execution_bound || executed > QN_MAX_EXECUTION_STEPS) {
            qn_diag_set_code(diag, "QN-E7593", 0, 0,
                             "function execution step bound exceeded");
            return QN_ERR_RUNTIME;
        }

        QNFunctionFrame *frame = &frames[depth];
        const QNInstruction *ins = &bc->instructions[pc];
        switch (ins->opcode) {
            case OP_U32_CONST:
                if (ins->a >= frame->scalar_count || frame->initialized[ins->a]) {
                    qn_diag_set_code(diag, "QN-E7596", 0, 0,
                                     "invalid function u32 constant destination");
                    return QN_ERR_RUNTIME;
                }
                frame->values[ins->a] = ins->imm;
                frame->initialized[ins->a] = true;
                ++pc;
                break;

            case OP_U32_ADD:
            case OP_U32_SUB:
            case OP_U32_MUL:
            case OP_U32_DIV:
                if (ins->a >= frame->scalar_count || ins->b >= frame->scalar_count ||
                    ins->flags >= frame->scalar_count || frame->initialized[ins->a] ||
                    !frame->initialized[ins->b] || !frame->initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7596", 0, 0,
                                     "function arithmetic reads invalid scalar state");
                    return QN_ERR_RUNTIME;
                }
                if (ins->opcode == OP_U32_DIV && frame->values[ins->flags] == 0u) {
                    qn_diag_set_code(diag, "QN-E7517", 0, 0,
                                     "u32 scalar division by zero");
                    return QN_ERR_RUNTIME;
                }
                switch (ins->opcode) {
                    case OP_U32_ADD:
                        frame->values[ins->a] =
                            frame->values[ins->b] + frame->values[ins->flags];
                        break;
                    case OP_U32_SUB:
                        frame->values[ins->a] =
                            frame->values[ins->b] - frame->values[ins->flags];
                        break;
                    case OP_U32_MUL:
                        frame->values[ins->a] =
                            frame->values[ins->b] * frame->values[ins->flags];
                        break;
                    case OP_U32_DIV:
                        frame->values[ins->a] =
                            frame->values[ins->b] / frame->values[ins->flags];
                        break;
                    default:
                        break;
                }
                frame->initialized[ins->a] = true;
                ++pc;
                break;

            case OP_CALL: {
                if (ins->imm >= bc->function_count || ins->a >= frame->scalar_count ||
                    frame->initialized[ins->a] || depth >= QN_MAX_CALL_DEPTH) {
                    qn_diag_set_code(diag, "QN-E7597", 0, 0,
                                     "invalid function CALL runtime state");
                    return QN_ERR_RUNTIME;
                }
                const QNFunctionRecord *callee = &bc->functions[ins->imm];
                uint32_t args[QN_MAX_FUNCTION_PARAMS] = {0u, 0u};
                if (callee->param_count >= 1u) {
                    if (ins->b >= frame->scalar_count || !frame->initialized[ins->b]) {
                        qn_diag_set_code(diag, "QN-E7597", 0, 0,
                                         "CALL reads uninitialized argument 0");
                        return QN_ERR_RUNTIME;
                    }
                    args[0] = frame->values[ins->b];
                }
                if (callee->param_count >= 2u) {
                    if (ins->flags >= frame->scalar_count ||
                        !frame->initialized[ins->flags]) {
                        qn_diag_set_code(diag, "QN-E7597", 0, 0,
                                         "CALL reads uninitialized argument 1");
                        return QN_ERR_RUNTIME;
                    }
                    args[1] = frame->values[ins->flags];
                }

                ++depth;
                QNFunctionFrame *next = &frames[depth];
                memset(next, 0, sizeof(*next));
                next->scalar_count = callee->scalar_count;
                next->return_pc = pc + 1u;
                next->return_destination = ins->a;
                next->function_index = (uint16_t)ins->imm;
                for (uint8_t i = 0u; i < callee->param_count; ++i) {
                    next->values[i] = args[i];
                    next->initialized[i] = true;
                }
                pc = callee->entry_pc;
                break;
            }

            case OP_RETURN: {
                if (depth == 0u || ins->a >= frame->scalar_count ||
                    !frame->initialized[ins->a] ||
                    frame->function_index >= bc->function_count) {
                    qn_diag_set_code(diag, "QN-E7598", 0, 0,
                                     "invalid function RETURN runtime state");
                    return QN_ERR_RUNTIME;
                }
                const QNFunctionRecord *fn = &bc->functions[frame->function_index];
                if (pc + 1u != fn->end_pc) {
                    qn_diag_set_code(diag, "QN-E7598", 0, 0,
                                     "RETURN is not terminal in function frame");
                    return QN_ERR_RUNTIME;
                }
                uint32_t value = frame->values[ins->a];
                size_t return_pc = frame->return_pc;
                uint8_t destination = frame->return_destination;
                memset(frame, 0, sizeof(*frame));
                --depth;
                QNFunctionFrame *caller = &frames[depth];
                if (destination >= caller->scalar_count ||
                    caller->initialized[destination]) {
                    qn_diag_set_code(diag, "QN-E7598", 0, 0,
                                     "invalid caller return destination");
                    return QN_ERR_RUNTIME;
                }
                caller->values[destination] = value;
                caller->initialized[destination] = true;
                pc = return_pc;
                break;
            }

            case OP_U32_EMIT:
                if (depth != 0u || emitted || ins->a >= frame->scalar_count ||
                    !frame->initialized[ins->a]) {
                    qn_diag_set_code(diag, "QN-E7599", 0, 0,
                                     "invalid function-program emit state");
                    return QN_ERR_RUNTIME;
                }
                emitted = true;
                emitted_id = ins->a;
                emitted_value = frame->values[ins->a];
                ++pc;
                break;

            case OP_END:
                if (depth != 0u || pc + 1u != bc->instruction_count || !emitted) {
                    qn_diag_set_code(diag, "QN-E7599", 0, 0,
                                     "invalid function-program termination");
                    return QN_ERR_RUNTIME;
                }
                terminated = true;
                pc = bc->instruction_count;
                break;

            default:
                qn_diag_set_code(diag, "QN-E7596", 0, 0,
                                 "unexpected opcode in Step6 function VM");
                return QN_ERR_RUNTIME;
        }
    }

    if (!terminated || !emitted || depth != 0u) {
        qn_diag_set_code(diag, "QN-E7599", 0, 0,
                         "function program did not terminate through main END");
        return QN_ERR_RUNTIME;
    }
    qn_finish_u32_scalar_result(bc, route, out, emitted_id, emitted_value);
    return QN_OK;
}

static QNStatus qn_vm_run_typed_scalar(
    const QNBytecode *bc,
    uint32_t shots,
    uint64_t seed,
    const QNGpuQvmRoute *route,
    const QNRuntimeInputs *runtime_inputs,
    QNRunResult *out,
    QNDiagnostic *diag
) {
    if (shots != 0u || seed != 0u) {
        qn_diag_set_code(diag, "QN-E7510", 0, 0,
                         "typed scalar programs do not accept --shots or --seed");
        return QN_ERR_PARSE;
    }
    if (strcmp(route->selected_backend, "cpu") != 0) {
        qn_diag_set_code(diag, "QN-E7511", 0, 0,
                         "typed scalar execution requires CPU route");
        return QN_ERR_RUNTIME;
    }
    if (!qn_qbc_is_typed_scalar_program(bc)) {
        qn_diag_set_code(diag, "QN-E7512", 0, 0,
                         "invalid typed scalar bytecode contract");
        return QN_ERR_QBC;
    }
    if (qn_qbc_has_runtime_inputs(bc) || qn_qbc_has_functions(bc)) {
        return qn_vm_run_function_scalar(
            bc, route, runtime_inputs, out, diag
        );
    }
    if (runtime_inputs && runtime_inputs->provided) {
        qn_diag_set_code(diag, "QN-E7617", 0, 0,
                         "runtime inputs are not accepted by this QBC program");
        return QN_ERR_PARSE;
    }

    uint32_t values[QN_MAX_SCALARS] = {0};
    bool initialized[QN_MAX_SCALARS] = {false};
    bool emitted = false;
    bool terminated = false;
    uint16_t emitted_id = 0u;
    size_t pc = 0u;
    uint64_t executed = 0u;
    uint64_t execution_bound = 0u;
    bool repeat_active = false;
    uint32_t repeat_remaining = 0u;
    size_t repeat_enter_pc = SIZE_MAX;

    if (!qn_qbc_execution_step_bound(bc, &execution_bound)) {
        qn_diag_set_code(diag, "QN-E7565", 0, 0,
                         "typed scalar execution budget is invalid or exceeds %u",
                         QN_MAX_EXECUTION_STEPS);
        return QN_ERR_LIMIT;
    }

    while (pc < bc->instruction_count) {
        if (++executed > execution_bound ||
            executed > QN_MAX_EXECUTION_STEPS) {
            qn_diag_set_code(diag, "QN-E7565", 0, 0,
                             "typed scalar execution step bound exceeded");
            return QN_ERR_RUNTIME;
        }

        const QNInstruction *ins = &bc->instructions[pc];
        switch (ins->opcode) {
            case OP_U32_CONST:
                values[ins->a] = ins->imm;
                initialized[ins->a] = true;
                ++pc;
                break;
            case OP_U32_ADD:
                if (!initialized[ins->b] || !initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7513", 0, 0,
                                     "u32 scalar bytecode reads uninitialized value");
                    return QN_ERR_RUNTIME;
                }
                values[ins->a] = values[ins->b] + values[ins->flags];
                initialized[ins->a] = true;
                ++pc;
                break;
            case OP_U32_SUB:
                if (!initialized[ins->b] || !initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7513", 0, 0,
                                     "u32 scalar bytecode reads uninitialized value");
                    return QN_ERR_RUNTIME;
                }
                values[ins->a] = values[ins->b] - values[ins->flags];
                initialized[ins->a] = true;
                ++pc;
                break;
            case OP_U32_MUL:
                if (!initialized[ins->b] || !initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7513", 0, 0,
                                     "u32 scalar bytecode reads uninitialized value");
                    return QN_ERR_RUNTIME;
                }
                values[ins->a] = values[ins->b] * values[ins->flags];
                initialized[ins->a] = true;
                ++pc;
                break;
            case OP_U32_DIV:
                if (!initialized[ins->b] || !initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7513", 0, 0,
                                     "u32 scalar bytecode reads uninitialized value");
                    return QN_ERR_RUNTIME;
                }
                if (values[ins->flags] == 0u) {
                    qn_diag_set_code(diag, "QN-E7517", 0, 0,
                                     "u32 scalar division by zero");
                    return QN_ERR_RUNTIME;
                }
                values[ins->a] = values[ins->b] / values[ins->flags];
                initialized[ins->a] = true;
                ++pc;
                break;
            case OP_U32_EQ:
            case OP_U32_NE:
            case OP_U32_LT:
            case OP_U32_LE:
            case OP_U32_GT:
            case OP_U32_GE:
                if (!initialized[ins->b] || !initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7513", 0, 0,
                                     "typed scalar bytecode reads uninitialized value");
                    return QN_ERR_RUNTIME;
                }
                switch (ins->opcode) {
                    case OP_U32_EQ:
                        values[ins->a] = values[ins->b] == values[ins->flags];
                        break;
                    case OP_U32_NE:
                        values[ins->a] = values[ins->b] != values[ins->flags];
                        break;
                    case OP_U32_LT:
                        values[ins->a] = values[ins->b] < values[ins->flags];
                        break;
                    case OP_U32_LE:
                        values[ins->a] = values[ins->b] <= values[ins->flags];
                        break;
                    case OP_U32_GT:
                        values[ins->a] = values[ins->b] > values[ins->flags];
                        break;
                    case OP_U32_GE:
                        values[ins->a] = values[ins->b] >= values[ins->flags];
                        break;
                    default:
                        break;
                }
                initialized[ins->a] = true;
                ++pc;
                break;
            case OP_U32_SET_ADD:
            case OP_U32_SET_SUB:
            case OP_U32_SET_MUL:
            case OP_U32_SET_DIV:
                if (!initialized[ins->a] || !initialized[ins->b] ||
                    !initialized[ins->flags]) {
                    qn_diag_set_code(diag, "QN-E7568", 0, 0,
                                     "bounded repeat set reads uninitialized value");
                    return QN_ERR_RUNTIME;
                }
                if (ins->opcode == OP_U32_SET_DIV &&
                    values[ins->flags] == 0u) {
                    qn_diag_set_code(diag, "QN-E7517", 0, 0,
                                     "u32 scalar division by zero");
                    return QN_ERR_RUNTIME;
                }
                switch (ins->opcode) {
                    case OP_U32_SET_ADD:
                        values[ins->a] = values[ins->b] + values[ins->flags];
                        break;
                    case OP_U32_SET_SUB:
                        values[ins->a] = values[ins->b] - values[ins->flags];
                        break;
                    case OP_U32_SET_MUL:
                        values[ins->a] = values[ins->b] * values[ins->flags];
                        break;
                    case OP_U32_SET_DIV:
                        values[ins->a] = values[ins->b] / values[ins->flags];
                        break;
                    default:
                        break;
                }
                ++pc;
                break;
            case OP_REPEAT_ENTER: {
                uint32_t repeat_count = (uint32_t)ins->a |
                                        ((uint32_t)ins->b << 8);
                if (repeat_count == 0u ||
                    repeat_count > QN_MAX_REPEAT_ITERATIONS ||
                    ins->imm <= pc + 1u ||
                    ins->imm >= bc->instruction_count) {
                    qn_diag_set_code(diag, "QN-E7569", 0, 0,
                                     "invalid runtime repeat enter state");
                    return QN_ERR_RUNTIME;
                }
                if (!repeat_active) {
                    repeat_active = true;
                    repeat_remaining = repeat_count;
                    repeat_enter_pc = pc;
                } else if (repeat_enter_pc != pc || repeat_remaining == 0u) {
                    qn_diag_set_code(diag, "QN-E7569", 0, 0,
                                     "invalid runtime repeat re-entry state");
                    return QN_ERR_RUNTIME;
                }
                ++pc;
                break;
            }
            case OP_REPEAT_NEXT:
                if (!repeat_active || repeat_remaining == 0u ||
                    ins->imm != repeat_enter_pc || ins->imm >= pc) {
                    qn_diag_set_code(diag, "QN-E7569", 0, 0,
                                     "invalid runtime repeat next state");
                    return QN_ERR_RUNTIME;
                }
                if (repeat_remaining > 1u) {
                    --repeat_remaining;
                    pc = (size_t)ins->imm;
                } else {
                    repeat_remaining = 0u;
                    repeat_active = false;
                    repeat_enter_pc = SIZE_MAX;
                    ++pc;
                }
                break;
            case OP_U32_EMIT:
            case OP_BOOL_EMIT:
                if (!initialized[ins->a] || emitted) {
                    qn_diag_set_code(diag, "QN-E7514", 0, 0,
                                     "invalid typed scalar emit state");
                    return QN_ERR_RUNTIME;
                }
                emitted = true;
                emitted_id = ins->a;
                ++pc;
                break;
            case OP_JUMP_IF_FALSE:
                if (!initialized[ins->a] || values[ins->a] > 1u ||
                    ins->imm <= pc ||
                    ins->imm >= bc->instruction_count) {
                    qn_diag_set_code(diag, "QN-E7542", 0, 0,
                                     "invalid runtime conditional jump state");
                    return QN_ERR_RUNTIME;
                }
                pc = values[ins->a] == 0u
                    ? (size_t)ins->imm
                    : pc + 1u;
                break;
            case OP_JUMP:
                if (ins->imm <= pc ||
                    ins->imm >= bc->instruction_count) {
                    qn_diag_set_code(diag, "QN-E7542", 0, 0,
                                     "invalid runtime forward jump target");
                    return QN_ERR_RUNTIME;
                }
                pc = (size_t)ins->imm;
                break;
            case OP_END:
                if (pc + 1u != bc->instruction_count || !emitted ||
                    repeat_active) {
                    qn_diag_set_code(diag, "QN-E7515", 0, 0,
                                     "invalid typed scalar program termination");
                    return QN_ERR_RUNTIME;
                }
                terminated = true;
                pc = bc->instruction_count;
                break;
            default:
                qn_diag_set_code(diag, "QN-E7516", 0, 0,
                                 "unexpected opcode in typed scalar VM");
                return QN_ERR_RUNTIME;
        }
    }

    if (!terminated || !emitted) {
        qn_diag_set_code(diag, "QN-E7515", 0, 0,
                         "typed scalar program did not terminate through END");
        return QN_ERR_RUNTIME;
    }

    out->native_scalar_result = true;
    out->shots = 1u;
    out->seed = 0u;
    out->scalar_output_id = emitted_id;
    out->scalar_output_value = values[emitted_id];
    out->scalar_output_is_bool =
        (bc->scalar_bool_mask & (UINT64_C(1) << emitted_id)) != 0u;
    out->qvm_gpu_execution_attempted = false;
    out->qvm_gpu_execution_completed = false;
    out->qvm_cpu_fallback = route->cpu_fallback;

    if (out->scalar_output_is_bool) {
        uint8_t encoded = (uint8_t)(out->scalar_output_value != 0u);
        qn_sha256(&encoded, 1u, out->scalar_output_digest);
    } else {
        uint8_t encoded[4] = {
            (uint8_t)out->scalar_output_value,
            (uint8_t)(out->scalar_output_value >> 8),
            (uint8_t)(out->scalar_output_value >> 16),
            (uint8_t)(out->scalar_output_value >> 24)
        };
        qn_sha256(encoded, sizeof(encoded), out->scalar_output_digest);
    }
    return QN_OK;
}

QNStatus qn_vm_run_guarded_with_inputs(const QNBytecode *bc,
                                       uint32_t shots,
                                       uint64_t seed,
                                       const QNGuardPolicy *policy,
                                       const QNGpuQvmRoute *route,
                                       const QNRuntimeInputs *runtime_inputs,
                                       QNRunResult *out,
                                       QNDiagnostic *diag) {
    if (!bc || !policy || !route || !out) {
        qn_diag_set_code(
            diag,
            "QN-E7413",
            0,
            0,
            "invalid routed VM execution arguments"
        );
        return QN_ERR_RUNTIME;
    }

    QNStatus status = qn_vm_prepare_result(
        bc,
        policy,
        route,
        out,
        diag
    );
    if (status != QN_OK) {
        return status;
    }

    if (qn_qbc_is_bounded_u32_vector_add(bc)) {
        status = qn_vm_run_bounded_compute(
            bc,
            shots,
            seed,
            route,
            out,
            diag
        );
        if (status != QN_OK) {
            qn_run_result_free(out);
        }
        return status;
    }

    if (qn_qbc_is_typed_scalar_program(bc)) {
        status = qn_vm_run_typed_scalar(
            bc, shots, seed, route, runtime_inputs, out, diag
        );
        if (status != QN_OK) qn_run_result_free(out);
        return status;
    }

    if (strcmp(route->selected_backend, "cpu") != 0) {
        qn_diag_set_code(
            diag,
            "QN-E7414",
            0,
            0,
            "quantum-state simulation requires CPU route"
        );
        qn_run_result_free(out);
        return QN_ERR_RUNTIME;
    }

    if (!shots) shots = bc->default_shots;
    if (!seed) seed = bc->default_seed;
    if (!shots || shots > QN_MAX_SHOTS) {
        qn_diag_set(diag, 0, 0,
                    "shots must be 1..%u", QN_MAX_SHOTS);
        qn_run_result_free(out);
        return QN_ERR_LIMIT;
    }

    size_t dim = (size_t)1 << bc->total_qubits;
    double complex *amp = calloc(dim, sizeof(*amp));
    if (!amp) {
        qn_diag_set(diag, 0, 0,
                    "cannot allocate state vector for %u qubits",
                    bc->total_qubits);
        qn_run_result_free(out);
        return QN_ERR_RUNTIME;
    }

    out->shots = shots;
    out->seed = seed;
    RNG rng = {seed};

    for (uint32_t shot = 0; shot < shots; ++shot) {
        memset(amp, 0, dim * sizeof(*amp));
        amp[bc->initial_basis] = 1.0 + 0.0 * I;
        bool measured = false;
        uint64_t state = 0;

        for (size_t i = 0; i < bc->instruction_count; ++i) {
            const QNInstruction *in = &bc->instructions[i];
            switch (in->opcode) {
                case OP_H: gate_h(amp, dim, in->a); break;
                case OP_X: gate_x(amp, dim, in->a); break;
                case OP_Z: gate_z(amp, dim, in->a); break;
                case OP_CX:
                    gate_cx(amp, dim, in->a, in->b);
                    break;
                case OP_MEASURE_ALL:
                    state = measure_all(amp, dim, &rng);
                    measured = true;
                    break;
                case OP_EMIT:
                    break;
                case OP_END:
                    i = bc->instruction_count;
                    break;
                case OP_U32_VECTOR_ADD:
                case OP_U32_CONST:
                case OP_U32_ADD:
                case OP_U32_SUB:
                case OP_U32_MUL:
                case OP_U32_DIV:
                case OP_U32_EQ:
                case OP_U32_NE:
                case OP_U32_LT:
                case OP_U32_LE:
                case OP_U32_GT:
                case OP_U32_GE:
                case OP_U32_EMIT:
                case OP_BOOL_EMIT:
                case OP_JUMP_IF_FALSE:
                case OP_JUMP:
                case OP_U32_SET_ADD:
                case OP_U32_SET_SUB:
                case OP_U32_SET_MUL:
                case OP_U32_SET_DIV:
                case OP_REPEAT_ENTER:
                case OP_REPEAT_NEXT:
                case OP_CALL:
                case OP_RETURN:
                    qn_diag_set_code(
                        diag,
                        "QN-E7415",
                        0,
                        0,
                        "native compute opcode reached quantum VM"
                    );
                    free(amp);
                    qn_run_result_free(out);
                    return QN_ERR_RUNTIME;
                default:
                    qn_diag_set(diag, 0, 0,
                                "unknown opcode 0x%02x", in->opcode);
                    free(amp);
                    qn_run_result_free(out);
                    return QN_ERR_RUNTIME;
            }
        }

        if (!measured) {
            qn_diag_set(diag, 0, 0,
                        "bytecode ended without measurement");
            free(amp);
            qn_run_result_free(out);
            return QN_ERR_RUNTIME;
        }

        if (!hist_add(out, state)) {
            qn_diag_set(diag, 0, 0,
                        "out of memory building histogram");
            free(amp);
            qn_run_result_free(out);
            return QN_ERR_RUNTIME;
        }
    }

    free(amp);
    return QN_OK;
}

static void print_bits(FILE *f, uint64_t state, unsigned n) {
    fputc('|',f);
    for(int i=(int)n-1;i>=0;i--) fputc((state>>i)&1u?'1':'0',f);
    fputc('>',f);
}

static void qn_print_approval_lines(const QNRunResult *result,
                                    FILE *stream) {
    char approved_text[256];
    qn_capability_format(
        result->approved_capabilities,
        approved_text,
        sizeof(approved_text)
    );
    fprintf(stream, "approved_capabilities=%s\n", approved_text);
    fprintf(
        stream,
        "approval_scheme=%s\n",
        result->approval_scheme[0]
            ? result->approval_scheme
            : "none"
    );

    if (result->has_approval_digest) {
        char approval_hex[65];
        qn_hex32(result->approval_digest, approval_hex);
        fprintf(stream, "approval_token_sha256=%s\n", approval_hex);
    } else {
        fprintf(stream, "approval_token_sha256=none\n");
    }

    if (result->has_approval_issuer) {
        char issuer_hex[65];
        qn_hex32(
            result->approval_issuer_fingerprint,
            issuer_hex
        );
        fprintf(
            stream,
            "approval_issuer_fingerprint=%s\n",
            issuer_hex
        );
    } else {
        fprintf(stream, "approval_issuer_fingerprint=none\n");
    }

    if (result->approval_revocation_checked) {
        fprintf(stream, "approval_revocation=checked-clear\n");
        fprintf(
            stream,
            "approval_token_revoked=%s\n",
            result->approval_token_revoked ? "true" : "false"
        );
        fprintf(
            stream,
            "approval_issuer_revoked=%s\n",
            result->approval_issuer_revoked ? "true" : "false"
        );
    } else {
        fprintf(stream, "approval_revocation=not-applicable\n");
        fprintf(stream, "approval_token_revoked=not-applicable\n");
        fprintf(stream, "approval_issuer_revoked=not-applicable\n");
    }

    fprintf(
        stream,
        "approval_replay=%s\n",
        result->approval_replay_consumed
            ? "consumed"
            : "not-applicable"
    );
}

static void qn_print_route_lines(const QNRunResult *result,
                                 FILE *stream) {
    fprintf(
        stream,
        "qvm_backend_schema=QBIT_NOVA_QVM_GPU_ROUTING_V06\n"
    );
    fprintf(
        stream,
        "qvm_requested_backend=%s\n",
        result->qvm_requested_backend
    );
    fprintf(
        stream,
        "qvm_selected_backend=%s\n",
        result->qvm_selected_backend
    );
    fprintf(
        stream,
        "qvm_selection_reason=%s\n",
        result->qvm_selection_reason
    );
    fprintf(stream, "qvm_operation=%s\n", result->qvm_operation);
    fprintf(
        stream,
        "qvm_gpu_eligible=%s\n",
        result->qvm_gpu_eligible ? "true" : "false"
    );
    fprintf(
        stream,
        "qvm_gpu_execution_attempted=%s\n",
        result->qvm_gpu_execution_attempted ? "true" : "false"
    );
    fprintf(
        stream,
        "qvm_gpu_execution_completed=%s\n",
        result->qvm_gpu_execution_completed ? "true" : "false"
    );
    fprintf(
        stream,
        "qvm_cpu_fallback=%s\n",
        result->qvm_cpu_fallback ? "true" : "false"
    );
}

static unsigned qn_scalar_bool_count(const QNBytecode *bc) {
    unsigned count = 0u;
    uint64_t mask = bc->scalar_bool_mask;
    while (mask) {
        count += (unsigned)(mask & UINT64_C(1));
        mask >>= 1;
    }
    return count;
}

QNStatus qn_vm_run_guarded(const QNBytecode *bc,
                           uint32_t shots,
                           uint64_t seed,
                           const QNGuardPolicy *policy,
                           const QNGpuQvmRoute *route,
                           QNRunResult *out,
                           QNDiagnostic *diag) {
    return qn_vm_run_guarded_with_inputs(
        bc, shots, seed, policy, route, NULL, out, diag
    );
}

void qn_print_result(const QNBytecode *bc,
                     const QNRunResult *result,
                     FILE *stream) {
    char qbc_hex[65];
    char source_hex[65];
    qn_hex32(result->qbc_digest, qbc_hex);
    qn_hex32(bc->source_digest, source_hex);

    char capability_text[256];
    qn_capability_format(
        bc->capability_mask,
        capability_text,
        sizeof(capability_text)
    );

    if (result->native_scalar_result) {
        char output_hex[65];
        unsigned bool_count = qn_scalar_bool_count(bc);
        unsigned u32_count = (unsigned)bc->scalar_count - bool_count;
        qn_hex32(result->scalar_output_digest, output_hex);
        if (qn_qbc_has_runtime_inputs(bc)) {
            char input_hex[65];
            uint64_t step_bound = 0u;
            qn_hex32(result->runtime_input_digest, input_hex);
            (void)qn_qbc_execution_step_bound(bc, &step_bound);
            fprintf(stream, "QBIT_NOVA_NATIVE_RUNTIME_INPUT_RUN_V07_STEP7\n");
            fprintf(stream, "boundary=native_typed_u32_runtime_inputs\n");
            fprintf(stream, "physical_qpu=false\n");
            fprintf(stream, "qubits=0\n");
            fprintf(stream, "function_count=%u\n", bc->function_count);
            fprintf(stream, "main_scalar_slots=%u\n", bc->scalar_count);
            fprintf(stream, "source_sha256=%s\n", source_hex);
            fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
            fprintf(stream, "capabilities=%s\n", capability_text);
            fprintf(stream, "guard=allowed\n");
            qn_print_approval_lines(result, stream);
            qn_print_route_lines(result, stream);
            fprintf(stream, "scalar_contract=typed-u32-runtime-inputs-v1\n");
            fprintf(stream, "qbc_version=9\n");
            fprintf(stream, "runtime_input_abi=%u\n", result->runtime_input_abi);
            fprintf(stream, "runtime_input_count=%u\n", result->runtime_input_count);
            fprintf(stream, "runtime_input_digest=%s\n", input_hex);
            fprintf(stream, "runtime_input_values_redacted=true\n");
            if (bc->function_count > 0u) {
                fprintf(stream, "function_table_record_size=12\n");
                fprintf(stream, "call_opcode=0x66\n");
                fprintf(stream, "return_opcode=0x67\n");
                fprintf(stream, "parameter_passing=by-value\n");
            }
            fprintf(stream, "execution_step_bound=%llu\n",
                    (unsigned long long)step_bound);
            fprintf(stream, "execution_step_limit=%u\n", QN_MAX_EXECUTION_STEPS);
            fprintf(stream, "implicit_type_conversion=false\n");
            fprintf(stream, "emitted_scalar_id=%u\n", result->scalar_output_id);
            fprintf(stream, "emitted_u32=%u\n", result->scalar_output_value);
            fprintf(stream, "output_sha256=%s\n", output_hex);
            return;
        }
        if (qn_qbc_has_functions(bc)) {
            uint64_t step_bound = 0u;
            (void)qn_qbc_execution_step_bound(bc, &step_bound);
            fprintf(stream, "QBIT_NOVA_NATIVE_FUNCTION_RUN_V07_STEP6\n");
            fprintf(stream, "boundary=native_typed_u32_functions\n");
            fprintf(stream, "physical_qpu=false\n");
            fprintf(stream, "qubits=0\n");
            fprintf(stream, "function_count=%u\n", bc->function_count);
            fprintf(stream, "main_scalar_slots=%u\n", bc->scalar_count);
            fprintf(stream, "source_sha256=%s\n", source_hex);
            fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
            fprintf(stream, "capabilities=%s\n", capability_text);
            fprintf(stream, "guard=allowed\n");
            qn_print_approval_lines(result, stream);
            qn_print_route_lines(result, stream);
            fprintf(stream, "scalar_contract=typed-u32-functions-v1\n");
            fprintf(stream, "qbc_version=8\n");
            fprintf(stream, "function_table_record_size=12\n");
            fprintf(stream, "call_opcode=0x66\n");
            fprintf(stream, "return_opcode=0x67\n");
            fprintf(stream, "max_functions=%u\n", QN_MAX_FUNCTIONS);
            fprintf(stream, "max_function_params=%u\n", QN_MAX_FUNCTION_PARAMS);
            fprintf(stream, "max_call_depth=%u\n", QN_MAX_CALL_DEPTH);
            fprintf(stream, "recursion=false\n");
            fprintf(stream, "parameter_passing=by-value\n");
            fprintf(stream, "execution_step_bound=%llu\n",
                    (unsigned long long)step_bound);
            fprintf(stream, "execution_step_limit=%u\n", QN_MAX_EXECUTION_STEPS);
            fprintf(stream, "implicit_type_conversion=false\n");
            fprintf(stream, "emitted_scalar_id=%u\n", result->scalar_output_id);
            fprintf(stream, "emitted_u32=%u\n", result->scalar_output_value);
            fprintf(stream, "output_sha256=%s\n", output_hex);
            return;
        }
        if (qn_qbc_has_bounded_repeat(bc)) {
            uint32_t repeat_count = 0u;
            uint64_t step_bound = 0u;
            for (size_t i = 0; i < bc->instruction_count; ++i) {
                if (bc->instructions[i].opcode == OP_REPEAT_ENTER) {
                    repeat_count = (uint32_t)bc->instructions[i].a |
                                   ((uint32_t)bc->instructions[i].b << 8);
                    break;
                }
            }
            (void)qn_qbc_execution_step_bound(bc, &step_bound);
            fprintf(stream, "QBIT_NOVA_NATIVE_BOUNDED_REPEAT_RUN_V07_STEP5\n");
            fprintf(stream, "boundary=native_typed_u32_bounded_repeat\n");
            fprintf(stream, "physical_qpu=false\n");
            fprintf(stream, "qubits=0\n");
            fprintf(stream, "scalar_slots=%u\n", bc->scalar_count);
            fprintf(stream, "u32_scalars=%u\n", u32_count);
            fprintf(stream, "bool_scalars=%u\n", bool_count);
            fprintf(stream, "source_sha256=%s\n", source_hex);
            fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
            fprintf(stream, "capabilities=%s\n", capability_text);
            fprintf(stream, "guard=allowed\n");
            qn_print_approval_lines(result, stream);
            qn_print_route_lines(result, stream);
            fprintf(stream, "scalar_contract=typed-u32-bounded-repeat-v1\n");
            fprintf(stream, "control_flow=bounded-repeat-only\n");
            fprintf(stream, "general_backward_jump=false\n");
            fprintf(stream, "repeat_iterations=%u\n", repeat_count);
            fprintf(stream, "repeat_max_iterations=%u\n", QN_MAX_REPEAT_ITERATIONS);
            fprintf(stream, "execution_step_bound=%llu\n",
                    (unsigned long long)step_bound);
            fprintf(stream, "execution_step_limit=%u\n", QN_MAX_EXECUTION_STEPS);
            fprintf(stream, "mutation_ops=set-add,set-sub,set-mul,set-div\n");
            fprintf(stream, "repeat_enter_opcode=0x64\n");
            fprintf(stream, "repeat_next_opcode=0x65\n");
            fprintf(stream, "implicit_type_conversion=false\n");
            fprintf(stream, "emitted_scalar_id=%u\n", result->scalar_output_id);
            fprintf(stream, "output_type=%s\n",
                    result->scalar_output_is_bool ? "bool" : "u32");
            if (result->scalar_output_is_bool) {
                fprintf(stream, "emitted_bool=%s\n",
                        result->scalar_output_value ? "true" : "false");
            } else {
                fprintf(stream, "emitted_u32=%u\n", result->scalar_output_value);
            }
            fprintf(stream, "output_sha256=%s\n", output_hex);
            return;
        }
        if (bc->scalar_bool_mask != 0u) {
            bool control_flow = qn_qbc_has_control_flow(bc);
            fprintf(stream, "%s\n", control_flow
                ? "QBIT_NOVA_NATIVE_TYPED_CONTROL_FLOW_RUN_V07_STEP4"
                : "QBIT_NOVA_NATIVE_TYPED_SCALAR_RUN_V07_STEP3");
            fprintf(stream, "boundary=%s\n", control_flow
                ? "native_typed_u32_bool_control_flow"
                : "native_typed_u32_bool_scalar");
            fprintf(stream, "physical_qpu=false\n");
            fprintf(stream, "qubits=0\n");
            fprintf(stream, "scalar_slots=%u\n", bc->scalar_count);
            fprintf(stream, "u32_scalars=%u\n", u32_count);
            fprintf(stream, "bool_scalars=%u\n", bool_count);
            fprintf(stream, "source_sha256=%s\n", source_hex);
            fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
            fprintf(stream, "capabilities=%s\n", capability_text);
            fprintf(stream, "guard=allowed\n");
            qn_print_approval_lines(result, stream);
            qn_print_route_lines(result, stream);
            fprintf(stream, "scalar_contract=%s\n", control_flow
                ? "typed-u32-bool-ifelse-v1"
                : "typed-u32-bool-v1");
            if (control_flow) {
                fprintf(stream, "control_flow=if-else-forward-only\n");
                fprintf(stream, "jump_if_false_opcode=0x5e\n");
                fprintf(stream, "jump_opcode=0x5f\n");
            }
            fprintf(stream, "comparison_ops=eq,ne,lt,le,gt,ge\n");
            fprintf(stream, "comparison_operand_type=u32\n");
            fprintf(stream, "comparison_result_type=bool\n");
            fprintf(stream, "bool_encoding=canonical-0-or-1\n");
            fprintf(stream, "implicit_type_conversion=false\n");
            fprintf(stream, "emitted_scalar_id=%u\n", result->scalar_output_id);
            fprintf(stream, "output_type=%s\n",
                    result->scalar_output_is_bool ? "bool" : "u32");
            if (result->scalar_output_is_bool) {
                fprintf(stream, "emitted_bool=%s\n",
                        result->scalar_output_value ? "true" : "false");
            } else {
                fprintf(stream, "emitted_u32=%u\n",
                        result->scalar_output_value);
            }
            fprintf(stream, "output_sha256=%s\n", output_hex);
            return;
        }
        fprintf(stream, "QBIT_NOVA_NATIVE_SCALAR_RUN_V07\n");
        fprintf(stream, "boundary=native_typed_u32_scalar\n");
        fprintf(stream, "physical_qpu=false\n");
        fprintf(stream, "qubits=0\n");
        fprintf(stream, "u32_scalars=%u\n", u32_count);
        fprintf(stream, "source_sha256=%s\n", source_hex);
        fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
        fprintf(stream, "capabilities=%s\n", capability_text);
        fprintf(stream, "guard=allowed\n");
        qn_print_approval_lines(result, stream);
        qn_print_route_lines(result, stream);
        fprintf(stream, "scalar_contract=typed-u32-scalar-v1\n");
        fprintf(stream, "arithmetic_ops=add,sub,mul,div\n");
        fprintf(stream, "overflow_semantics=uint32-modulo\n");
        fprintf(stream, "division_semantics=unsigned-integer-truncate\n");
        fprintf(stream, "division_by_zero=fail-closed-QN-E7517\n");
        fprintf(stream, "implicit_type_conversion=false\n");
        fprintf(stream, "emitted_scalar_id=%u\n", result->scalar_output_id);
        fprintf(stream, "emitted_u32=%u\n", result->scalar_output_value);
        fprintf(stream, "output_sha256=%s\n", output_hex);
        return;
    }

    if (result->native_compute_result) {
        char shader_hex[65];
        char output_hex[65];
        qn_hex32(result->compute_shader_digest, shader_hex);
        qn_hex32(result->compute_output_digest, output_hex);

        fprintf(stream, "QBIT_NOVA_NATIVE_COMPUTE_RUN_V06\n");
        fprintf(stream, "boundary=native_bounded_compute\n");
        fprintf(stream, "physical_qpu=false\n");
        fprintf(stream, "qubits=0\n");
        fprintf(stream, "source_sha256=%s\n", source_hex);
        fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
        fprintf(stream, "capabilities=%s\n", capability_text);
        fprintf(stream, "guard=allowed\n");
        qn_print_approval_lines(result, stream);
        qn_print_route_lines(result, stream);
        fprintf(stream, "compute_contract=bounded-u32-vector-add-v1\n");
        fprintf(stream, "input_contract=deterministic-fixed-v1\n");
        fprintf(
            stream,
            "element_count=%u\n",
            result->compute_element_count
        );
        fprintf(stream, "integer_width=32\n");
        fprintf(stream, "overflow_semantics=uint32-modulo\n");
        fprintf(
            stream,
            "hardware_device=%s\n",
            result->compute_hardware_device[0]
                ? result->compute_hardware_device
                : "none"
        );
        fprintf(
            stream,
            "hardware_vendor_id=0x%04x\n",
            result->compute_hardware_vendor_id
        );
        fprintf(
            stream,
            "cpu_reference_validated=%s\n",
            result->compute_cpu_reference_validated
                ? "true"
                : "false"
        );
        fprintf(
            stream,
            "result_match=%s\n",
            result->compute_result_match ? "true" : "false"
        );
        fprintf(stream, "shader_sha256=%s\n", shader_hex);
        fprintf(stream, "output_sha256=%s\n", output_hex);
        return;
    }

    fprintf(stream, "QBIT_NOVA_NATIVE_RUN_V05\n");
    fprintf(stream, "boundary=software_virtual_qcpu\n");
    fprintf(stream, "physical_qpu=false\n");
    fprintf(
        stream,
        "qubits=%u\nshots=%u\nseed=%llu\n",
        bc->total_qubits,
        result->shots,
        (unsigned long long)result->seed
    );
    fprintf(stream, "source_sha256=%s\n", source_hex);
    fprintf(stream, "qbc_sha256=%s\n", qbc_hex);
    fprintf(stream, "capabilities=%s\n", capability_text);
    fprintf(stream, "guard=allowed\n");
    qn_print_approval_lines(result, stream);
    qn_print_route_lines(result, stream);

    for (size_t i = 0; i < result->entry_count; ++i) {
        print_bits(stream, result->entries[i].state, bc->total_qubits);
        fprintf(
            stream,
            "=%llu\n",
            (unsigned long long)result->entries[i].count
        );
    }
}

static void qn_write_approval_json(FILE *stream,
                                   const QNRunResult *result) {
    char approved_text[256];
    qn_capability_format(
        result->approved_capabilities,
        approved_text,
        sizeof(approved_text)
    );
    fprintf(
        stream,
        "  \"approved_capabilities\": \"%s\",\n",
        approved_text
    );
    fprintf(
        stream,
        "  \"approval_scheme\": \"%s\",\n",
        result->approval_scheme[0]
            ? result->approval_scheme
            : "none"
    );

    if (result->has_approval_digest) {
        char approval_hex[65];
        qn_hex32(result->approval_digest, approval_hex);
        fprintf(
            stream,
            "  \"approval_token_sha256\": \"%s\",\n",
            approval_hex
        );
    } else {
        fprintf(stream, "  \"approval_token_sha256\": null,\n");
    }

    if (result->has_approval_issuer) {
        char issuer_hex[65];
        qn_hex32(
            result->approval_issuer_fingerprint,
            issuer_hex
        );
        fprintf(
            stream,
            "  \"approval_issuer_fingerprint\": \"%s\",\n",
            issuer_hex
        );
    } else {
        fprintf(stream, "  \"approval_issuer_fingerprint\": null,\n");
    }

    fprintf(
        stream,
        "  \"approval_revocation\": \"%s\",\n",
        result->approval_revocation_checked
            ? "checked-clear"
            : "not-applicable"
    );

    if (result->approval_revocation_checked) {
        fprintf(
            stream,
            "  \"approval_token_revoked\": %s,\n",
            result->approval_token_revoked ? "true" : "false"
        );
        fprintf(
            stream,
            "  \"approval_issuer_revoked\": %s,\n",
            result->approval_issuer_revoked ? "true" : "false"
        );
    } else {
        fprintf(stream, "  \"approval_token_revoked\": null,\n");
        fprintf(stream, "  \"approval_issuer_revoked\": null,\n");
    }

    fprintf(
        stream,
        "  \"approval_replay\": \"%s\",\n",
        result->approval_replay_consumed
            ? "consumed"
            : "not-applicable"
    );
}

static void qn_write_route_json(FILE *stream,
                                const QNRunResult *result) {
    fprintf(
        stream,
        "  \"qvm_backend_schema\": "
        "\"QBIT_NOVA_QVM_GPU_ROUTING_V06\",\n"
    );
    fprintf(
        stream,
        "  \"qvm_requested_backend\": \"%s\",\n",
        result->qvm_requested_backend
    );
    fprintf(
        stream,
        "  \"qvm_selected_backend\": \"%s\",\n",
        result->qvm_selected_backend
    );
    fprintf(
        stream,
        "  \"qvm_selection_reason\": \"%s\",\n",
        result->qvm_selection_reason
    );
    fprintf(
        stream,
        "  \"qvm_operation\": \"%s\",\n",
        result->qvm_operation
    );
    fprintf(
        stream,
        "  \"qvm_gpu_eligible\": %s,\n",
        result->qvm_gpu_eligible ? "true" : "false"
    );
    fprintf(
        stream,
        "  \"qvm_gpu_execution_attempted\": %s,\n",
        result->qvm_gpu_execution_attempted ? "true" : "false"
    );
    fprintf(
        stream,
        "  \"qvm_gpu_execution_completed\": %s,\n",
        result->qvm_gpu_execution_completed ? "true" : "false"
    );
    fprintf(
        stream,
        "  \"qvm_cpu_fallback\": %s,\n",
        result->qvm_cpu_fallback ? "true" : "false"
    );
}

static QNStatus qn_write_runtime_input_receipt(
    FILE *stream,
    const QNBytecode *bc,
    const QNRunResult *result
) {
    char qbc_hex[65];
    char source_hex[65];
    char output_hex[65];
    char input_hex[65];
    char capability_text[256];
    uint64_t step_bound = 0u;

    qn_hex32(result->qbc_digest, qbc_hex);
    qn_hex32(bc->source_digest, source_hex);
    qn_hex32(result->scalar_output_digest, output_hex);
    qn_hex32(result->runtime_input_digest, input_hex);
    qn_capability_format(bc->capability_mask,
                         capability_text,
                         sizeof(capability_text));
    (void)qn_qbc_execution_step_bound(bc, &step_bound);

    fprintf(stream, "{\n");
    fprintf(stream, "  \"marker\": \"QBIT_NOVA_NATIVE_RUNTIME_INPUT_RECEIPT_V07_STEP7\",\n");
    fprintf(stream, "  \"creator\": \"Universal Dragon Aslam\",\n");
    fprintf(stream, "  \"boundary\": \"native_typed_u32_runtime_inputs\",\n");
    fprintf(stream, "  \"physical_qpu\": false,\n");
    fprintf(stream, "  \"guard\": \"allowed\",\n");
    fprintf(stream, "  \"capabilities\": \"%s\",\n", capability_text);
    qn_write_approval_json(stream, result);
    qn_write_route_json(stream, result);
    fprintf(stream, "  \"scalar_contract\": \"typed-u32-runtime-inputs-v1\",\n");
    fprintf(stream, "  \"qbc_version\": 9,\n");
    fprintf(stream, "  \"runtime_input_abi\": %u,\n",
            result->runtime_input_abi);
    fprintf(stream, "  \"runtime_input_count\": %u,\n",
            result->runtime_input_count);
    fprintf(stream, "  \"runtime_input_digest\": \"%s\",\n", input_hex);
    fprintf(stream, "  \"runtime_input_values_redacted\": true,\n");
    fprintf(stream, "  \"function_count\": %u,\n", bc->function_count);
    if (bc->function_count > 0u) {
        fprintf(stream, "  \"function_table_record_size\": 12,\n");
        fprintf(stream, "  \"main_entry_pc\": %u,\n", bc->main_entry_pc);
        fprintf(stream, "  \"call_opcode\": \"0x66\",\n");
        fprintf(stream, "  \"return_opcode\": \"0x67\",\n");
        fprintf(stream, "  \"max_functions\": %u,\n", QN_MAX_FUNCTIONS);
        fprintf(stream, "  \"max_function_params\": %u,\n",
                QN_MAX_FUNCTION_PARAMS);
        fprintf(stream, "  \"max_call_depth\": %u,\n", QN_MAX_CALL_DEPTH);
        fprintf(stream, "  \"recursion\": false,\n");
        fprintf(stream, "  \"parameter_passing\": \"by-value\",\n");
    }
    fprintf(stream, "  \"execution_step_bound\": %llu,\n",
            (unsigned long long)step_bound);
    fprintf(stream, "  \"execution_step_limit\": %u,\n",
            QN_MAX_EXECUTION_STEPS);
    fprintf(stream, "  \"implicit_type_conversion\": false,\n");
    fprintf(stream, "  \"scalar_slots\": %u,\n", bc->scalar_count);
    fprintf(stream, "  \"emitted_scalar_id\": %u,\n",
            result->scalar_output_id);
    fprintf(stream, "  \"output_type\": \"u32\",\n");
    fprintf(stream, "  \"emitted_u32\": %u,\n",
            result->scalar_output_value);
    fprintf(stream, "  \"output_sha256\": \"%s\",\n", output_hex);
    fprintf(stream, "  \"qubits\": 0,\n");
    fprintf(stream, "  \"source_sha256\": \"%s\",\n", source_hex);
    fprintf(stream, "  \"qbc_sha256\": \"%s\"\n", qbc_hex);
    fprintf(stream, "}\n");
    return QN_OK;
}

static QNStatus qn_write_typed_scalar_receipt(
    FILE *stream,
    const QNBytecode *bc,
    const QNRunResult *result
) {
    char qbc_hex[65];
    char source_hex[65];
    char output_hex[65];
    char capability_text[256];
    unsigned bool_count = qn_scalar_bool_count(bc);
    unsigned u32_count = (unsigned)bc->scalar_count - bool_count;
    qn_hex32(result->qbc_digest, qbc_hex);
    qn_hex32(bc->source_digest, source_hex);
    qn_hex32(result->scalar_output_digest, output_hex);
    qn_capability_format(bc->capability_mask,
                         capability_text,
                         sizeof(capability_text));

    fprintf(stream, "{\n");
    bool functions = qn_qbc_has_functions(bc);
    bool bounded_repeat = qn_qbc_has_bounded_repeat(bc);
    bool control_flow = qn_qbc_has_control_flow(bc);
    fprintf(stream, "  \"marker\": \"%s\",\n",
            functions
                ? "QBIT_NOVA_NATIVE_FUNCTION_RECEIPT_V07_STEP6"
                : (bounded_repeat
                    ? "QBIT_NOVA_NATIVE_BOUNDED_REPEAT_RECEIPT_V07_STEP5"
                    : (control_flow
                        ? "QBIT_NOVA_NATIVE_TYPED_CONTROL_FLOW_RECEIPT_V07_STEP4"
                        : "QBIT_NOVA_NATIVE_TYPED_SCALAR_RECEIPT_V07_STEP3")));
    fprintf(stream, "  \"creator\": \"Universal Dragon Aslam\",\n");
    fprintf(stream, "  \"boundary\": \"%s\",\n",
            functions
                ? "native_typed_u32_functions"
                : (bounded_repeat
                    ? "native_typed_u32_bounded_repeat"
                    : (control_flow
                        ? "native_typed_u32_bool_control_flow"
                        : "native_typed_u32_bool_scalar")));
    fprintf(stream, "  \"physical_qpu\": false,\n");
    fprintf(stream, "  \"guard\": \"allowed\",\n");
    fprintf(stream, "  \"capabilities\": \"%s\",\n", capability_text);
    qn_write_approval_json(stream, result);
    qn_write_route_json(stream, result);
    fprintf(stream, "  \"scalar_contract\": \"%s\",\n",
            functions
                ? "typed-u32-functions-v1"
                : (bounded_repeat
                    ? "typed-u32-bounded-repeat-v1"
                    : (control_flow
                        ? "typed-u32-bool-ifelse-v1"
                        : "typed-u32-bool-v1")));
    if (functions) {
        uint64_t step_bound = 0u;
        (void)qn_qbc_execution_step_bound(bc, &step_bound);
        fprintf(stream, "  \"qbc_version\": 8,\n");
        fprintf(stream, "  \"function_count\": %u,\n", bc->function_count);
        fprintf(stream, "  \"function_table_record_size\": 12,\n");
        fprintf(stream, "  \"main_entry_pc\": %u,\n", bc->main_entry_pc);
        fprintf(stream, "  \"call_opcode\": \"0x66\",\n");
        fprintf(stream, "  \"return_opcode\": \"0x67\",\n");
        fprintf(stream, "  \"max_functions\": %u,\n", QN_MAX_FUNCTIONS);
        fprintf(stream, "  \"max_function_params\": %u,\n", QN_MAX_FUNCTION_PARAMS);
        fprintf(stream, "  \"max_call_depth\": %u,\n", QN_MAX_CALL_DEPTH);
        fprintf(stream, "  \"recursion\": false,\n");
        fprintf(stream, "  \"parameter_passing\": \"by-value\",\n");
        fprintf(stream, "  \"execution_step_bound\": %llu,\n",
                (unsigned long long)step_bound);
        fprintf(stream, "  \"execution_step_limit\": %u,\n", QN_MAX_EXECUTION_STEPS);
    } else if (bounded_repeat) {
        uint32_t repeat_count = 0u;
        uint64_t step_bound = 0u;
        for (size_t i = 0; i < bc->instruction_count; ++i) {
            if (bc->instructions[i].opcode == OP_REPEAT_ENTER) {
                repeat_count = (uint32_t)bc->instructions[i].a |
                               ((uint32_t)bc->instructions[i].b << 8);
                break;
            }
        }
        (void)qn_qbc_execution_step_bound(bc, &step_bound);
        fprintf(stream, "  \"control_flow\": \"bounded-repeat-only\",\n");
        fprintf(stream, "  \"general_backward_jump\": false,\n");
        fprintf(stream, "  \"repeat_iterations\": %u,\n", repeat_count);
        fprintf(stream, "  \"repeat_max_iterations\": %u,\n",
                QN_MAX_REPEAT_ITERATIONS);
        fprintf(stream, "  \"execution_step_bound\": %llu,\n",
                (unsigned long long)step_bound);
        fprintf(stream, "  \"execution_step_limit\": %u,\n",
                QN_MAX_EXECUTION_STEPS);
        fprintf(stream, "  \"mutation_ops\": \"set-add,set-sub,set-mul,set-div\",\n");
        fprintf(stream, "  \"repeat_enter_opcode\": \"0x64\",\n");
        fprintf(stream, "  \"repeat_next_opcode\": \"0x65\",\n");
    } else if (control_flow) {
        fprintf(stream, "  \"control_flow\": \"if-else-forward-only\",\n");
        fprintf(stream, "  \"jump_if_false_opcode\": \"0x5e\",\n");
        fprintf(stream, "  \"jump_opcode\": \"0x5f\",\n");
    }
    if (!functions) {
        fprintf(stream, "  \"comparison_ops\": \"eq,ne,lt,le,gt,ge\",\n");
        fprintf(stream, "  \"comparison_operand_type\": \"u32\",\n");
        fprintf(stream, "  \"comparison_result_type\": \"bool\",\n");
        fprintf(stream, "  \"bool_encoding\": \"canonical-0-or-1\",\n");
    }
    fprintf(stream, "  \"implicit_type_conversion\": false,\n");
    fprintf(stream, "  \"scalar_slots\": %u,\n", bc->scalar_count);
    fprintf(stream, "  \"u32_scalars\": %u,\n", u32_count);
    fprintf(stream, "  \"bool_scalars\": %u,\n", bool_count);
    fprintf(stream, "  \"emitted_scalar_id\": %u,\n", result->scalar_output_id);
    fprintf(stream, "  \"output_type\": \"%s\",\n",
            result->scalar_output_is_bool ? "bool" : "u32");
    if (result->scalar_output_is_bool) {
        fprintf(stream, "  \"emitted_bool\": %s,\n",
                result->scalar_output_value ? "true" : "false");
    } else {
        fprintf(stream, "  \"emitted_u32\": %u,\n",
                result->scalar_output_value);
    }
    fprintf(stream, "  \"output_sha256\": \"%s\",\n", output_hex);
    fprintf(stream, "  \"qubits\": 0,\n");
    fprintf(stream, "  \"source_sha256\": \"%s\",\n", source_hex);
    fprintf(stream, "  \"qbc_sha256\": \"%s\"\n", qbc_hex);
    fprintf(stream, "}\n");
    return QN_OK;
}

static QNStatus qn_write_scalar_receipt(
    FILE *stream,
    const QNBytecode *bc,
    const QNRunResult *result
) {
    char qbc_hex[65];
    char source_hex[65];
    char output_hex[65];
    char capability_text[256];
    qn_hex32(result->qbc_digest, qbc_hex);
    qn_hex32(bc->source_digest, source_hex);
    qn_hex32(result->scalar_output_digest, output_hex);
    qn_capability_format(bc->capability_mask,
                         capability_text,
                         sizeof(capability_text));

    fprintf(stream, "{\n");
    fprintf(stream, "  \"marker\": \"QBIT_NOVA_NATIVE_SCALAR_RECEIPT_V07\",\n");
    fprintf(stream, "  \"creator\": \"Universal Dragon Aslam\",\n");
    fprintf(stream, "  \"boundary\": \"native_typed_u32_scalar\",\n");
    fprintf(stream, "  \"physical_qpu\": false,\n");
    fprintf(stream, "  \"guard\": \"allowed\",\n");
    fprintf(stream, "  \"capabilities\": \"%s\",\n", capability_text);
    qn_write_approval_json(stream, result);
    qn_write_route_json(stream, result);
    fprintf(stream, "  \"scalar_contract\": \"typed-u32-scalar-v1\",\n");
    fprintf(stream, "  \"arithmetic_ops\": \"add,sub,mul,div\",\n");
    fprintf(stream, "  \"integer_width\": 32,\n");
    fprintf(stream, "  \"overflow_semantics\": \"uint32-modulo\",\n");
    fprintf(stream, "  \"division_semantics\": \"unsigned-integer-truncate\",\n");
    fprintf(stream, "  \"division_by_zero\": \"fail-closed-QN-E7517\",\n");
    fprintf(stream, "  \"implicit_type_conversion\": false,\n");
    fprintf(stream, "  \"u32_scalars\": %u,\n",
            (unsigned)bc->scalar_count - qn_scalar_bool_count(bc));
    fprintf(stream, "  \"emitted_scalar_id\": %u,\n", result->scalar_output_id);
    fprintf(stream, "  \"emitted_u32\": %u,\n", result->scalar_output_value);
    fprintf(stream, "  \"output_sha256\": \"%s\",\n", output_hex);
    fprintf(stream, "  \"qubits\": 0,\n");
    fprintf(stream, "  \"source_sha256\": \"%s\",\n", source_hex);
    fprintf(stream, "  \"qbc_sha256\": \"%s\"\n", qbc_hex);
    fprintf(stream, "}\n");
    return QN_OK;
}

static QNStatus qn_write_compute_receipt(
    FILE *stream,
    const QNBytecode *bc,
    const QNRunResult *result
) {
    char qbc_hex[65];
    char source_hex[65];
    char shader_hex[65];
    char output_hex[65];
    char capability_text[256];

    qn_hex32(result->qbc_digest, qbc_hex);
    qn_hex32(bc->source_digest, source_hex);
    qn_hex32(result->compute_shader_digest, shader_hex);
    qn_hex32(result->compute_output_digest, output_hex);
    qn_capability_format(
        bc->capability_mask,
        capability_text,
        sizeof(capability_text)
    );

    fprintf(stream, "{\n");
    fprintf(
        stream,
        "  \"marker\": \"QBIT_NOVA_NATIVE_COMPUTE_RECEIPT_V06\",\n"
    );
    fprintf(stream, "  \"creator\": \"Universal Dragon Aslam\",\n");
    fprintf(stream, "  \"boundary\": \"native_bounded_compute\",\n");
    fprintf(stream, "  \"physical_qpu\": false,\n");
    fprintf(stream, "  \"guard\": \"allowed\",\n");
    fprintf(
        stream,
        "  \"capabilities\": \"%s\",\n",
        capability_text
    );
    qn_write_approval_json(stream, result);
    qn_write_route_json(stream, result);
    fprintf(
        stream,
        "  \"compute_contract\": \"bounded-u32-vector-add-v1\",\n"
    );
    fprintf(
        stream,
        "  \"input_contract\": \"deterministic-fixed-v1\",\n"
    );
    fprintf(
        stream,
        "  \"element_count\": %u,\n",
        result->compute_element_count
    );
    fprintf(stream, "  \"integer_width\": 32,\n");
    fprintf(stream, "  \"overflow_semantics\": \"uint32-modulo\",\n");
    fprintf(
        stream,
        "  \"hardware_device\": \"%s\",\n",
        result->compute_hardware_device[0]
            ? result->compute_hardware_device
            : "none"
    );
    fprintf(
        stream,
        "  \"hardware_vendor_id\": \"0x%04x\",\n",
        result->compute_hardware_vendor_id
    );
    fprintf(
        stream,
        "  \"cpu_reference_validated\": %s,\n",
        result->compute_cpu_reference_validated ? "true" : "false"
    );
    fprintf(
        stream,
        "  \"result_match\": %s,\n",
        result->compute_result_match ? "true" : "false"
    );
    fprintf(
        stream,
        "  \"shader_sha256\": \"%s\",\n",
        shader_hex
    );
    fprintf(
        stream,
        "  \"output_sha256\": \"%s\",\n",
        output_hex
    );
    fprintf(stream, "  \"qubits\": 0,\n");
    fprintf(stream, "  \"source_sha256\": \"%s\",\n", source_hex);
    fprintf(stream, "  \"qbc_sha256\": \"%s\"\n", qbc_hex);
    fprintf(stream, "}\n");
    return QN_OK;
}

QNStatus qn_write_receipt(const char *path,
                          const QNBytecode *bc,
                          const QNRunResult *result,
                          QNDiagnostic *diag) {
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        qn_diag_set(diag, 0, 0,
                    "cannot create receipt '%s'", path);
        return QN_ERR_IO;
    }

    if (result->native_scalar_result &&
        qn_qbc_has_runtime_inputs(bc)) {
        qn_write_runtime_input_receipt(stream, bc, result);
    } else if (result->native_scalar_result &&
        (bc->scalar_bool_mask != 0u || qn_qbc_has_bounded_repeat(bc) ||
         qn_qbc_has_functions(bc))) {
        qn_write_typed_scalar_receipt(stream, bc, result);
    } else if (result->native_scalar_result) {
        qn_write_scalar_receipt(stream, bc, result);
    } else if (result->native_compute_result) {
        qn_write_compute_receipt(stream, bc, result);
    } else {
        char qbc_hex[65];
        char source_hex[65];
        char capability_text[256];
        qn_hex32(result->qbc_digest, qbc_hex);
        qn_hex32(bc->source_digest, source_hex);
        qn_capability_format(
            bc->capability_mask,
            capability_text,
            sizeof(capability_text)
        );

        fprintf(stream, "{\n");
        fprintf(
            stream,
            "  \"marker\": \"QBIT_NOVA_NATIVE_RECEIPT_V05\",\n"
        );
        fprintf(stream, "  \"creator\": \"Universal Dragon Aslam\",\n");
        fprintf(stream, "  \"boundary\": \"software_virtual_qcpu\",\n");
        fprintf(stream, "  \"physical_qpu\": false,\n");
        fprintf(stream, "  \"guard\": \"allowed\",\n");
        fprintf(
            stream,
            "  \"capabilities\": \"%s\",\n",
            capability_text
        );
        qn_write_approval_json(stream, result);
        qn_write_route_json(stream, result);
        fprintf(stream, "  \"qubits\": %u,\n", bc->total_qubits);
        fprintf(stream, "  \"shots\": %u,\n", result->shots);
        fprintf(
            stream,
            "  \"seed\": %llu,\n",
            (unsigned long long)result->seed
        );
        fprintf(
            stream,
            "  \"source_sha256\": \"%s\",\n",
            source_hex
        );
        fprintf(
            stream,
            "  \"qbc_sha256\": \"%s\",\n",
            qbc_hex
        );
        fprintf(stream, "  \"histogram\": {");
        for (size_t i = 0; i < result->entry_count; ++i) {
            if (i) fprintf(stream, ", ");
            fprintf(stream, "\"");
            for (int q = (int)bc->total_qubits - 1; q >= 0; --q) {
                fputc(
                    (result->entries[i].state >> q) & 1u
                        ? '1'
                        : '0',
                    stream
                );
            }
            fprintf(
                stream,
                "\": %llu",
                (unsigned long long)result->entries[i].count
            );
        }
        fprintf(stream, "}\n}\n");
    }

    if (fclose(stream) != 0) {
        qn_diag_set(diag, 0, 0, "failed closing receipt");
        return QN_ERR_IO;
    }

    return QN_OK;
}
