#include "qn_vm.h"

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

QNStatus qn_vm_run_guarded(const QNBytecode *bc,
                           uint32_t shots,
                           uint64_t seed,
                           const QNGuardPolicy *policy,
                           QNRunResult *out,
                           QNDiagnostic *diag) {
    memset(out,0,sizeof(*out));

    QNStatus guard_status =
        qn_guard_enforce(bc->capability_mask, policy, diag);
    if (guard_status != QN_OK) return guard_status;

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

    if(!shots) shots=bc->default_shots;
    if(!seed) seed=bc->default_seed;
    if(!shots || shots>QN_MAX_SHOTS) {
        qn_diag_set(diag,0,0,"shots must be 1..%u",QN_MAX_SHOTS);
        return QN_ERR_LIMIT;
    }
    size_t dim=(size_t)1<<bc->total_qubits;
    double complex *amp=calloc(dim,sizeof(*amp));
    if(!amp){ qn_diag_set(diag,0,0,"cannot allocate state vector for %u qubits",bc->total_qubits); return QN_ERR_RUNTIME; }

    uint8_t *encoded=NULL; size_t encoded_size=0;
    if(qn_qbc_encode(bc,&encoded,&encoded_size,diag)!=QN_OK){ free(amp); return QN_ERR_QBC; }
    qn_sha256(encoded,encoded_size,out->qbc_digest);
    free(encoded);

    out->shots=shots; out->seed=seed;
    RNG rng={seed};

    for(uint32_t shot=0;shot<shots;shot++) {
        memset(amp,0,dim*sizeof(*amp));
        amp[bc->initial_basis]=1.0+0.0*I;
        bool measured=false;
        uint64_t state=0;

        for(size_t i=0;i<bc->instruction_count;i++) {
            const QNInstruction *in=&bc->instructions[i];
            switch(in->opcode) {
                case OP_H: gate_h(amp,dim,in->a); break;
                case OP_X: gate_x(amp,dim,in->a); break;
                case OP_Z: gate_z(amp,dim,in->a); break;
                case OP_CX: gate_cx(amp,dim,in->a,in->b); break;
                case OP_MEASURE_ALL: state=measure_all(amp,dim,&rng); measured=true; break;
                case OP_EMIT: break;
                case OP_END: i=bc->instruction_count; break;
                default:
                    qn_diag_set(diag,0,0,"unknown opcode 0x%02x",in->opcode);
                    free(amp); qn_run_result_free(out); return QN_ERR_RUNTIME;
            }
        }
        if(!measured) {
            qn_diag_set(diag,0,0,"bytecode ended without measurement");
            free(amp); qn_run_result_free(out); return QN_ERR_RUNTIME;
        }
        if(!hist_add(out,state)) {
            qn_diag_set(diag,0,0,"out of memory building histogram");
            free(amp); qn_run_result_free(out); return QN_ERR_RUNTIME;
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

void qn_print_result(const QNBytecode *bc, const QNRunResult *result, FILE *stream) {
    char qbc_hex[65], source_hex[65];
    qn_hex32(result->qbc_digest,qbc_hex);
    qn_hex32(bc->source_digest,source_hex);
    fprintf(stream,"QBIT_NOVA_NATIVE_RUN_V05\n");
    fprintf(stream,"boundary=software_virtual_qcpu\n");
    fprintf(stream,"physical_qpu=false\n");
    fprintf(stream,"qubits=%u\nshots=%u\nseed=%llu\n",
            bc->total_qubits,result->shots,(unsigned long long)result->seed);
    fprintf(stream,"source_sha256=%s\nqbc_sha256=%s\n",source_hex,qbc_hex);
    char capability_text[256];
    qn_capability_format(
        bc->capability_mask,
        capability_text,
        sizeof(capability_text)
    );
    fprintf(stream,"capabilities=%s\n",capability_text);
    fprintf(stream,"guard=allowed\n");
    char approved_text[256];
    qn_capability_format(
        result->approved_capabilities,
        approved_text,
        sizeof(approved_text)
    );
    fprintf(stream,"approved_capabilities=%s\n",approved_text);
    fprintf(stream,"approval_scheme=%s\n",
            result->approval_scheme[0]
                ? result->approval_scheme
                : "none");
    if (result->has_approval_digest) {
        char approval_hex[65];
        qn_hex32(result->approval_digest, approval_hex);
        fprintf(stream,"approval_token_sha256=%s\n",approval_hex);
    } else {
        fprintf(stream,"approval_token_sha256=none\n");
    }
    if (result->has_approval_issuer) {
        char issuer_hex[65];
        qn_hex32(
            result->approval_issuer_fingerprint,
            issuer_hex
        );
        fprintf(stream,
                "approval_issuer_fingerprint=%s\n",
                issuer_hex);
    } else {
        fprintf(stream,
                "approval_issuer_fingerprint=none\n");
    }
    if (result->approval_revocation_checked) {
        fprintf(stream,
                "approval_revocation=checked-clear\n");
        fprintf(stream,
                "approval_token_revoked=%s\n",
                result->approval_token_revoked
                    ? "true"
                    : "false");
        fprintf(stream,
                "approval_issuer_revoked=%s\n",
                result->approval_issuer_revoked
                    ? "true"
                    : "false");
    } else {
        fprintf(stream,
                "approval_revocation=not-applicable\n");
        fprintf(stream,
                "approval_token_revoked=not-applicable\n");
        fprintf(stream,
                "approval_issuer_revoked=not-applicable\n");
    }

    fprintf(stream,
            "approval_replay=%s\n",
            result->approval_replay_consumed
                ? "consumed"
                : "not-applicable");
    for(size_t i=0;i<result->entry_count;i++) {
        print_bits(stream,result->entries[i].state,bc->total_qubits);
        fprintf(stream,"=%llu\n",(unsigned long long)result->entries[i].count);
    }
}

QNStatus qn_write_receipt(const char *path, const QNBytecode *bc,
                          const QNRunResult *result, QNDiagnostic *diag) {
    FILE *f=fopen(path,"wb");
    if(!f){ qn_diag_set(diag,0,0,"cannot create receipt '%s'",path); return QN_ERR_IO; }
    char qbc_hex[65], source_hex[65];
    qn_hex32(result->qbc_digest,qbc_hex);
    qn_hex32(bc->source_digest,source_hex);
    fprintf(f,"{\n");
    fprintf(f,"  \"marker\": \"QBIT_NOVA_NATIVE_RECEIPT_V05\",\n");
    fprintf(f,"  \"creator\": \"Universal Dragon Aslam\",\n");
    fprintf(f,"  \"boundary\": \"software_virtual_qcpu\",\n");
    fprintf(f,"  \"physical_qpu\": false,\n");
    char capability_text[256];
    qn_capability_format(
        bc->capability_mask,
        capability_text,
        sizeof(capability_text)
    );
    fprintf(f,"  \"guard\": \"allowed\",\n");
    fprintf(f,"  \"capabilities\": \"%s\",\n",
            capability_text);
    char approved_text[256];
    qn_capability_format(
        result->approved_capabilities,
        approved_text,
        sizeof(approved_text)
    );
    fprintf(f,"  \"approved_capabilities\": \"%s\",\n",
            approved_text);
    fprintf(f,"  \"approval_scheme\": \"%s\",\n",
            result->approval_scheme[0]
                ? result->approval_scheme
                : "none");
    if (result->has_approval_digest) {
        char approval_hex[65];
        qn_hex32(result->approval_digest, approval_hex);
        fprintf(f,"  \"approval_token_sha256\": \"%s\",\n",
                approval_hex);
    } else {
        fprintf(f,"  \"approval_token_sha256\": null,\n");
    }
    if (result->has_approval_issuer) {
        char issuer_hex[65];
        qn_hex32(
            result->approval_issuer_fingerprint,
            issuer_hex
        );
        fprintf(f,
                "  \"approval_issuer_fingerprint\": \"%s\",\n",
                issuer_hex);
    } else {
        fprintf(f,
                "  \"approval_issuer_fingerprint\": null,\n");
    }
    fprintf(
        f,
        "  \"approval_revocation\": \"%s\",\n",
        result->approval_revocation_checked
            ? "checked-clear"
            : "not-applicable"
    );

    if (result->approval_revocation_checked) {
        fprintf(
            f,
            "  \"approval_token_revoked\": %s,\n",
            result->approval_token_revoked
                ? "true"
                : "false"
        );
        fprintf(
            f,
            "  \"approval_issuer_revoked\": %s,\n",
            result->approval_issuer_revoked
                ? "true"
                : "false"
        );
    } else {
        fprintf(
            f,
            "  \"approval_token_revoked\": null,\n"
        );
        fprintf(
            f,
            "  \"approval_issuer_revoked\": null,\n"
        );
    }

    fprintf(
        f,
        "  \"approval_replay\": \"%s\",\n",
        result->approval_replay_consumed
            ? "consumed"
            : "not-applicable"
    );
    fprintf(f,"  \"qubits\": %u,\n  \"shots\": %u,\n  \"seed\": %llu,\n",
            bc->total_qubits,result->shots,(unsigned long long)result->seed);
    fprintf(f,"  \"source_sha256\": \"%s\",\n  \"qbc_sha256\": \"%s\",\n",source_hex,qbc_hex);
    fprintf(f,"  \"histogram\": {");
    for(size_t i=0;i<result->entry_count;i++) {
        if(i) fprintf(f,", ");
        fprintf(f,"\"");
        for(int q=(int)bc->total_qubits-1;q>=0;q--) fputc((result->entries[i].state>>q)&1u?'1':'0',f);
        fprintf(f,"\": %llu",(unsigned long long)result->entries[i].count);
    }
    fprintf(f,"}\n}\n");
    if(fclose(f)!=0){ qn_diag_set(diag,0,0,"failed closing receipt"); return QN_ERR_IO; }
    return QN_OK;
}
