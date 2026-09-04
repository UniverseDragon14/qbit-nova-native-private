#include "qn_lexer.h"
#include "qn_parser.h"
#include "qn_qbc.h"
#include "qn_qir.h"
#include "qn_vm.h"
#include "qn_approval.h"
#include "qn_signed_approval.h"
#include "qn_trust_store.h"
#include "qn_replay_ledger.h"
#include "qn_revocation_store.h"
#include "qn_gpu_adapter.h"
#include "qn_gpu_compute.h"
#include "qn_gpu_routing.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *f) {
    fprintf(f,
        "QBIT NOVA Native v0.5.0\n"
        "Usage:\n"
        "  qnova lex <file.qn>\n"
        "  qnova check <file.qn>\n"
        "  qnova qir <file.qn>\n"
        "  qnova guard <capability> [--approve <capability>]\n"
        "  qnova approval keygen-ed25519 --private PRIVATE --public PUBLIC\n"
        "  qnova approval derive-public-ed25519 --private PRIVATE --public PUBLIC\n"
        "  qnova approval issue-ed25519 <file.qn> <capability>\n"
        "            --private-key PRIVATE --expires-at UNIX\n"
        "            [-o token.qns] [--issued-at UNIX]\n"
        "            [--nonce-hex 32HEX] [--context TEXT]\n"
        "  qnova approval verify-ed25519 <file.qn> <token.qns>\n"
        "            (--public-key PUBLIC | --trust-store-file STORE)\n"
        "            [--now UNIX]\n"
        "  qnova approval issue <file.qn> <capability> --key-file KEY\n"
        "            --expires-at UNIX [-o token.qna] [--issued-at UNIX]\n"
        "            [--nonce TEXT]\n"
        "  qnova approval verify <file.qn> <token.qna> --key-file KEY\n"
        "            [--now UNIX]\n"
        "  qnova gpu probe [--backend auto|cpu|vulkan]\n"
        "            [--receipt file.json]\n"
        "  qnova gpu compute-proof [--backend auto|cpu|vulkan]\n"
        "            [--receipt file.json]\n"
        "  qnova build <file.qn> -o <file.qbc>\n"
        "  qnova run <file.qn> [--shots N] [--seed N] [--policy safe|deny-all]\n"
        "            [--signed-approval-file token.qns]\n"
        "            [--approval-public-key-file PUBLIC]\n"
        "            [--trust-store-file STORE]\n"
        "            [--replay-ledger-file LEDGER]\n"
        "            [--revocation-store-file STORE]\n"
        "            [--approval-file token.qna --approval-key-file KEY]\n"
        "            [--backend cpu|auto|vulkan]\n"
        "            [--device-backend deny|mock|linux-gpio]\n"
        "            [--gpiochip /dev/gpiochipN] [--device-hold-ms 0..5000]\n"
        "            [--input name=decimal-u32]...\n"
        "            [--now UNIX] [--receipt file.json]\n"
        "  qnova exec <file.qbc> [same execution options]\n"
        "  qnova version\n");
}

static int print_diag(QNStatus st, const QNDiagnostic *d) {
    const char *code = d->code[0] ? d->code : "QN-E-GENERIC";
    if(d->line>0) {
        fprintf(stderr,"error[%s][%d:%d]: %s\n",
                code,d->line,d->column,d->message);
    } else {
        fprintf(stderr,"error[%s]: %s\n",code,d->message);
    }
    return (int)st;
}


static QNStatus parse_source_program(const char *path,
                                     QNProgram *program,
                                     uint8_t digest[32],
                                     QNDiagnostic *diag) {
    size_t size = 0;
    char *source = qn_read_text_file(path, &size, diag);
    if (!source) return QN_ERR_IO;

    qn_sha256((const uint8_t *)source, size, digest);

    QNTokenList tokens = {0};
    QNStatus status = qn_lex(source, &tokens, diag);
    free(source);
    if (status != QN_OK) return status;

    status = qn_parse(&tokens, program, diag);
    qn_tokens_free(&tokens);
    return status;
}

static QNStatus compile_source(const char *path,
                               QNBytecode *bc,
                               QNTokenList *tokens_out,
                               QNProgram *program_out,
                               QNDiagnostic *diag) {
    (void)tokens_out;

    QNProgram program = {0};
    uint8_t digest[32];
    QNStatus status =
        parse_source_program(path, &program, digest, diag);
    if (status != QN_OK) return status;

    status = qn_compile(&program, digest, bc, diag);

    if (program_out && status == QN_OK) {
        *program_out = program;
    } else {
        qn_program_free(&program);
    }

    return status;
}

static uint64_t parse_u64(const char *s, const char *name) {
    char *end=NULL;
    unsigned long long v=strtoull(s,&end,10);
    if(!s[0] || !end || *end){ fprintf(stderr,"invalid %s: %s\n",name,s); exit(QN_ERR_PARSE); }
    return (uint64_t)v;
}

typedef struct {
    uint32_t shots;
    uint64_t seed;
    bool shots_explicit;
    bool seed_explicit;
    const char *receipt;
    const char *approval_file;
    const char *approval_key_file;
    const char *signed_approval_file;
    const char *approval_public_key_file;
    const char *trust_store_file;
    const char *replay_ledger_file;
    const char *revocation_store_file;
    uint64_t approval_now;
    bool has_approval_now;
    QNGpuBackendRequest backend;
    bool backend_explicit;
    QNDeviceOptions device;
    bool device_backend_explicit;
    bool device_hold_explicit;
    const char *input_bindings[QN_MAX_RUNTIME_INPUTS];
    uint16_t input_binding_count;
    QNGuardPolicy policy;
} RunOptions;

static void parse_run_opts(int argc,
                           char **argv,
                           int from,
                           RunOptions *options) {
    memset(options, 0, sizeof(*options));
    options->backend = QN_GPU_BACKEND_CPU;
    qn_device_options_safe(&options->device);
    qn_guard_policy_safe(&options->policy);

    for(int i=from;i<argc;i++) {
        if(!strcmp(argv[i],"--shots") && i+1<argc) {
            options->shots=(uint32_t)parse_u64(argv[++i],"shots");
            options->shots_explicit=true;
        } else if(!strcmp(argv[i],"--seed") && i+1<argc) {
            options->seed=parse_u64(argv[++i],"seed");
            options->seed_explicit=true;
        } else if(!strcmp(argv[i],"--receipt") && i+1<argc) {
            options->receipt=argv[++i];
        } else if(!strcmp(argv[i],"--policy") && i+1<argc) {
            if(!qn_guard_policy_select(argv[++i],&options->policy)) {
                fprintf(stderr,"unknown policy: %s\n",argv[i]);
                exit(QN_ERR_PARSE);
            }
        } else if(!strcmp(argv[i],"--approval-file") && i+1<argc) {
            options->approval_file=argv[++i];
        } else if(!strcmp(argv[i],"--approval-key-file") && i+1<argc) {
            options->approval_key_file=argv[++i];
        } else if(!strcmp(argv[i],"--signed-approval-file") && i+1<argc) {
            options->signed_approval_file=argv[++i];
        } else if(!strcmp(argv[i],"--approval-public-key-file") && i+1<argc) {
            options->approval_public_key_file=argv[++i];
        } else if(!strcmp(argv[i],"--trust-store-file") && i+1<argc) {
            options->trust_store_file=argv[++i];
        } else if(!strcmp(argv[i],"--replay-ledger-file") && i+1<argc) {
            options->replay_ledger_file=argv[++i];
        } else if(!strcmp(argv[i],"--revocation-store-file") && i+1<argc) {
            options->revocation_store_file=argv[++i];
        } else if(!strcmp(argv[i],"--backend") && i+1<argc) {
            if(!qn_gpu_backend_parse(argv[++i],&options->backend)) {
                fprintf(stderr,"unknown backend: %s\n",argv[i]);
                exit(QN_ERR_PARSE);
            }
            options->backend_explicit=true;
        } else if(!strcmp(argv[i],"--device-backend") && i+1<argc) {
            if(!qn_device_backend_parse(argv[++i],&options->device.backend)) {
                fprintf(stderr,"unknown device backend: %s\n",argv[i]);
                exit(QN_ERR_PARSE);
            }
            options->device_backend_explicit=true;
        } else if(!strcmp(argv[i],"--gpiochip") && i+1<argc) {
            options->device.gpiochip_path=argv[++i];
        } else if(!strcmp(argv[i],"--device-hold-ms") && i+1<argc) {
            uint64_t hold=parse_u64(argv[++i],"device hold");
            if(hold>QN_MAX_DEVICE_HOLD_MS) {
                fprintf(stderr,"device hold must be 0..%u ms\n",
                        QN_MAX_DEVICE_HOLD_MS);
                exit(QN_ERR_LIMIT);
            }
            options->device.hold_ms=(uint32_t)hold;
            options->device_hold_explicit=true;
        } else if(!strcmp(argv[i],"--input") && i+1<argc) {
            if(options->input_binding_count >= QN_MAX_RUNTIME_INPUTS) {
                fprintf(stderr,"too many --input bindings (max %u)\n",
                        QN_MAX_RUNTIME_INPUTS);
                exit(QN_ERR_PARSE);
            }
            options->input_bindings[options->input_binding_count++] = argv[++i];
        } else if(!strcmp(argv[i],"--now") && i+1<argc) {
            options->approval_now=parse_u64(argv[++i],"now");
            options->has_approval_now=true;
        } else {
            fprintf(stderr,"unknown option: %s\n",argv[i]);
            exit(QN_ERR_PARSE);
        }
    }
}



static bool runtime_input_identifier_valid(const char *name) {
    if (!name || !name[0]) return false;
    unsigned char first = (unsigned char)name[0];
    if (!(isalpha(first) || first == '_')) return false;
    for (size_t i = 1u; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

static bool parse_runtime_u32(const char *text, uint32_t *value_out) {
    if (!text || !text[0] || !value_out) return false;
    uint64_t value = 0u;
    for (size_t i = 0u; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < '0' || c > '9') return false;
        value = value * UINT64_C(10) + (uint64_t)(c - '0');
        if (value > UINT32_MAX) return false;
    }
    *value_out = (uint32_t)value;
    return true;
}

static void runtime_input_digest(const QNBytecode *bc,
                                 QNRuntimeInputs *inputs) {
    static const uint8_t domain[] = "QN_RUNTIME_INPUT_V1";
    uint8_t canonical[
        (sizeof(domain) - 1u) +
        QN_MAX_RUNTIME_INPUTS * (2u + 32u + 4u)
    ];
    size_t at = 0u;

    memcpy(canonical + at, domain, sizeof(domain) - 1u);
    at += sizeof(domain) - 1u;

    for (uint16_t i = 0u; i < bc->input_count; ++i) {
        canonical[at++] = (uint8_t)i;
        canonical[at++] = (uint8_t)(i >> 8);
        memcpy(canonical + at, bc->inputs[i].input_name_sha256, 32);
        at += 32u;
        uint32_t value = inputs->values[i];
        canonical[at++] = (uint8_t)value;
        canonical[at++] = (uint8_t)(value >> 8);
        canonical[at++] = (uint8_t)(value >> 16);
        canonical[at++] = (uint8_t)(value >> 24);
    }

    qn_sha256(canonical, at, inputs->input_sha256);
}

static QNStatus resolve_runtime_inputs(const QNBytecode *bc,
                                       const RunOptions *options,
                                       QNRuntimeInputs *out,
                                       QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));

    if (!qn_qbc_has_runtime_inputs(bc)) {
        if (options->input_binding_count != 0u) {
            qn_diag_set_code(diag, "QN-E7617", 0, 0,
                             "--input is not accepted by this QBC program");
            return QN_ERR_PARSE;
        }
        return QN_OK;
    }

    if (options->input_binding_count != bc->input_count) {
        qn_diag_set_code(diag, "QN-E7618", 0, 0,
                         "runtime input binding count mismatch: expected %u, got %u",
                         bc->input_count, options->input_binding_count);
        return QN_ERR_PARSE;
    }

    bool seen[QN_MAX_RUNTIME_INPUTS] = {false};

    for (uint16_t binding = 0u;
         binding < options->input_binding_count;
         ++binding) {
        const char *text = options->input_bindings[binding];
        const char *eq = text ? strchr(text, '=') : NULL;
        if (!eq || eq == text || eq[1] == '\0') {
            qn_diag_set_code(diag, "QN-E7619", 0, 0,
                             "malformed runtime input binding");
            return QN_ERR_PARSE;
        }

        size_t name_len = (size_t)(eq - text);
        if (name_len == 0u || name_len >= QN_NAME_CAP) {
            qn_diag_set_code(diag, "QN-E7619", 0, 0,
                             "runtime input name is invalid");
            return QN_ERR_PARSE;
        }

        char name[QN_NAME_CAP];
        memcpy(name, text, name_len);
        name[name_len] = '\0';
        if (!runtime_input_identifier_valid(name)) {
            qn_diag_set_code(diag, "QN-E7619", 0, 0,
                             "runtime input name is not a valid identifier");
            return QN_ERR_PARSE;
        }

        uint32_t value = 0u;
        if (!parse_runtime_u32(eq + 1, &value)) {
            qn_diag_set_code(diag, "QN-E7620", 0, 0,
                             "runtime input value must be decimal u32");
            return QN_ERR_PARSE;
        }

        uint8_t name_digest[32];
        qn_sha256((const uint8_t *)name, strlen(name), name_digest);

        int matched = -1;
        for (uint16_t i = 0u; i < bc->input_count; ++i) {
            if (memcmp(name_digest, bc->inputs[i].input_name_sha256, 32) == 0) {
                matched = (int)i;
                break;
            }
        }

        if (matched < 0) {
            qn_diag_set_code(diag, "QN-E7621", 0, 0,
                             "unknown runtime input '%s'", name);
            return QN_ERR_PARSE;
        }

        if (seen[matched]) {
            qn_diag_set_code(diag, "QN-E7622", 0, 0,
                             "duplicate runtime input binding '%s'", name);
            return QN_ERR_PARSE;
        }

        seen[matched] = true;
        out->values[matched] = value;
    }

    for (uint16_t i = 0u; i < bc->input_count; ++i) {
        if (!seen[i]) {
            qn_diag_set_code(diag, "QN-E7623", 0, 0,
                             "missing runtime input binding at index %u", i);
            return QN_ERR_PARSE;
        }
    }

    out->count = bc->input_count;
    out->provided = true;
    runtime_input_digest(bc, out);
    return QN_OK;
}

static uint64_t current_unix_time(void) {
    time_t now = time(NULL);
    if (now < 0) {
        fprintf(stderr,"cannot read system time\n");
        exit(QN_ERR_RUNTIME);
    }
    return (uint64_t)now;
}


static QNStatus resolve_signed_approval_public_key(
    const char *public_key_path,
    const char *trust_store_path,
    const QNSignedApprovalToken *token,
    uint8_t public_key_out[QN_ED25519_PUBLIC_KEY_BYTES],
    QNDiagnostic *diag
) {
    bool has_public_key = public_key_path != NULL;
    bool has_trust_store = trust_store_path != NULL;

    if (!token ||
        !public_key_out ||
        has_public_key == has_trust_store) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "signed approval requires exactly one of "
            "--approval-public-key-file/--public-key "
            "or --trust-store-file"
        );
        return QN_ERR_RUNTIME;
    }

    if (has_public_key) {
        uint8_t *loaded = qn_signed_approval_load_key(
            public_key_path,
            QN_ED25519_PUBLIC_KEY_BYTES,
            diag
        );

        if (!loaded) {
            return QN_ERR_IO;
        }

        memcpy(
            public_key_out,
            loaded,
            QN_ED25519_PUBLIC_KEY_BYTES
        );

        free(loaded);
        return QN_OK;
    }

    QNTrustStore store;
    QNStatus status = qn_trust_store_load_file(
        trust_store_path,
        &store,
        diag
    );

    if (status != QN_OK) {
        return status;
    }

    return qn_trust_store_resolve(
        &store,
        token->issuer_fingerprint,
        public_key_out,
        diag
    );
}


static QNStatus apply_approval_to_policy(
    const RunOptions *options,
    const QNBytecode *bc,
    QNGuardPolicy *policy,
    QNDiagnostic *diag
) {
    bool has_hmac_file = options->approval_file != NULL;
    bool has_hmac_key = options->approval_key_file != NULL;
    bool has_signed_file = options->signed_approval_file != NULL;
    bool has_public_key = options->approval_public_key_file != NULL;
    bool has_trust_store = options->trust_store_file != NULL;
    bool has_replay_ledger = options->replay_ledger_file != NULL;
    bool has_revocation_store =
        options->revocation_store_file != NULL;

    if (has_hmac_file != has_hmac_key) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-011",
            0,
            0,
            "--approval-file and --approval-key-file must be used together"
        );
        return QN_ERR_RUNTIME;
    }

    unsigned int signed_key_sources =
        (has_public_key ? 1u : 0u) +
        (has_trust_store ? 1u : 0u);

    if (has_signed_file && signed_key_sources != 1u) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "--signed-approval-file requires exactly one of "
            "--approval-public-key-file or --trust-store-file"
        );
        return QN_ERR_RUNTIME;
    }

    if (!has_signed_file && signed_key_sources != 0u) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "signed approval key source requires "
            "--signed-approval-file"
        );
        return QN_ERR_RUNTIME;
    }

    if (has_signed_file != has_replay_ledger) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "Ed25519 execution requires both "
            "--signed-approval-file and "
            "--replay-ledger-file"
        );
        return QN_ERR_RUNTIME;
    }

    if (has_signed_file != has_revocation_store) {
        qn_diag_set_code(
            diag,
            "QN-E6109",
            0,
            0,
            "Ed25519 execution requires both "
            "--signed-approval-file and "
            "--revocation-store-file"
        );
        return QN_ERR_RUNTIME;
    }

    if (has_hmac_file && has_signed_file) {
        qn_diag_set_code(
            diag,
            "QN-E5004",
            0,
            0,
            "choose either HMAC approval or Ed25519 approval, not both"
        );
        return QN_ERR_RUNTIME;
    }

    uint64_t now = options->has_approval_now
        ? options->approval_now
        : current_unix_time();

    if (has_signed_file) {
        QNSignedApprovalToken token;
        QNStatus status = qn_signed_approval_read(
            options->signed_approval_file,
            &token,
            diag
        );
        if (status != QN_OK) return status;

        uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];

        status = resolve_signed_approval_public_key(
            options->approval_public_key_file,
            options->trust_store_file,
            &token,
            public_key,
            diag
        );

        if (status != QN_OK) return status;

        QNCapabilityMask approved = 0u;
        status = qn_signed_approval_verify(
            &token,
            bc->source_digest,
            bc->capability_mask,
            now,
            public_key,
            &approved,
            diag
        );

        if (status != QN_OK) return status;

        policy->approved |= approved;
        policy->has_approval_digest = true;
        memcpy(
            policy->approval_digest,
            token.token_digest,
            sizeof(policy->approval_digest)
        );
        snprintf(
            policy->approval_scheme,
            sizeof(policy->approval_scheme),
            "ed25519"
        );
        policy->has_approval_issuer = true;
        memcpy(
            policy->approval_issuer_fingerprint,
            token.issuer_fingerprint,
            sizeof(policy->approval_issuer_fingerprint)
        );
        return QN_OK;
    }

    if (!has_hmac_file) return QN_OK;

    QNApprovalToken token;
    QNStatus status =
        qn_approval_read(options->approval_file, &token, diag);
    if (status != QN_OK) return status;

    size_t key_size = 0;
    uint8_t *key = qn_approval_load_key(
        options->approval_key_file,
        &key_size,
        diag
    );
    if (!key) return QN_ERR_RUNTIME;

    QNCapabilityMask approved = 0;
    status = qn_approval_verify(
        &token,
        bc->source_digest,
        now,
        key,
        key_size,
        &approved,
        diag
    );
    free(key);

    if (status != QN_OK) return status;

    policy->approved |= approved;
    policy->has_approval_digest = true;
    memcpy(
        policy->approval_digest,
        token.token_digest,
        sizeof(policy->approval_digest)
    );
    snprintf(
        policy->approval_scheme,
        sizeof(policy->approval_scheme),
        "hmac-sha256"
    );
    return QN_OK;
}



static QNStatus check_revocation_before_replay(
    const RunOptions *options,
    const QNGuardPolicy *policy,
    QNDiagnostic *diag
) {
    if (!options || !policy) {
        qn_diag_set_code(
            diag,
            "QN-E6101",
            0,
            0,
            "invalid revocation execution arguments"
        );
        return QN_ERR_RUNTIME;
    }

    if (!options->signed_approval_file) {
        return QN_OK;
    }

    if (!options->revocation_store_file ||
        !policy->has_approval_digest ||
        !policy->has_approval_issuer) {
        qn_diag_set_code(
            diag,
            "QN-E6109",
            0,
            0,
            "verified Ed25519 execution requires "
            "revocation state"
        );
        return QN_ERR_RUNTIME;
    }

    QNRevocationStore store;
    QNStatus status = qn_revocation_store_load_file(
        options->revocation_store_file,
        &store,
        diag
    );

    if (status != QN_OK) {
        return status;
    }

    return qn_revocation_store_check(
        &store,
        policy->approval_digest,
        policy->approval_issuer_fingerprint,
        diag
    );
}


static QNStatus preflight_before_replay_consume(
    const QNBytecode *bc,
    uint32_t requested_shots,
    bool shots_explicit,
    bool seed_explicit,
    const QNGuardPolicy *policy,
    const QNDeviceOptions *device_options,
    bool device_backend_explicit,
    bool device_hold_explicit,
    QNDiagnostic *diag
) {
    QNStatus status = qn_guard_enforce(
        bc->capability_mask,
        policy,
        diag
    );

    if (status != QN_OK) {
        return status;
    }

    if (qn_qbc_is_gpio_output_program(bc)) {
        if (shots_explicit || seed_explicit) {
            qn_diag_set_code(diag, "QN-E7818", 0, 0,
                             "GPIO programs do not accept --shots or --seed");
            return QN_ERR_PARSE;
        }
        if (device_options &&
            device_options->backend == QN_DEVICE_BACKEND_LINUX_GPIO &&
            strcmp(policy->approval_scheme, "ed25519") != 0) {
            qn_diag_set_code(diag, "QN-E7820", 0, 0,
                             "physical GPIO requires Ed25519 approval");
            return QN_ERR_RUNTIME;
        }
        return qn_device_options_validate(device_options, true, diag);
    }

    if (device_backend_explicit || device_hold_explicit ||
        (device_options && device_options->gpiochip_path != NULL)) {
        qn_diag_set_code(diag, "QN-E7819", 0, 0,
                         "device options are accepted only by GPIO programs");
        return QN_ERR_PARSE;
    }

    if (qn_qbc_is_bounded_u32_vector_add(bc) ||
        qn_qbc_is_typed_scalar_program(bc)) {
        if (shots_explicit || seed_explicit) {
            bool scalar = qn_qbc_is_typed_scalar_program(bc);
            qn_diag_set_code(
                diag,
                scalar ? "QN-E7510" : "QN-E7410",
                0,
                0,
                scalar
                    ? "typed scalar programs do not accept --shots or --seed"
                    : "bounded vector-add does not accept --shots or --seed"
            );
            return QN_ERR_PARSE;
        }
        return QN_OK;
    }

    uint32_t shots = requested_shots
        ? requested_shots
        : bc->default_shots;

    if (!shots || shots > QN_MAX_SHOTS) {
        qn_diag_set_code(
            diag,
            "QN-E5302",
            0,
            0,
            "shots must be 1..%u",
            QN_MAX_SHOTS
        );
        return QN_ERR_LIMIT;
    }

    if (bc->total_qubits > QN_MAX_QUBITS ||
        bc->total_qubits >=
            (unsigned int)(sizeof(size_t) * 8u)) {
        qn_diag_set_code(
            diag,
            "QN-E5303",
            0,
            0,
            "qubit count exceeds VM addressable limit"
        );
        return QN_ERR_LIMIT;
    }

    return QN_OK;
}


int main(int argc,char **argv) {
    if(argc<2){ usage(stderr); return 1; }
    if(!strcmp(argv[1],"version")){
        printf("QBIT NOVA Native %d.%d.%d\n",QN_VERSION_MAJOR,QN_VERSION_MINOR,QN_VERSION_PATCH);
        printf("runtime=C17\npython_dependency=false\nboundary=software_virtual_qcpu,native_bounded_compute,bounded_gpio_output\n");
        return 0;
    }
    if(argc<3){ usage(stderr); return 1; }

    QNDiagnostic diag={0};

    if(!strcmp(argv[1],"gpu")) {
        const bool is_probe = strcmp(argv[2],"probe") == 0;
        const bool is_compute =
            strcmp(argv[2],"compute-proof") == 0;

        if(!is_probe && !is_compute) {
            qn_diag_set_code(
                &diag,
                "QN-E7002",
                0,
                0,
                "expected GPU subcommand: probe or compute-proof"
            );
            return print_diag(QN_ERR_PARSE,&diag);
        }

        QNGpuBackendRequest requested = QN_GPU_BACKEND_AUTO;
        const char *receipt_path = NULL;

        for(int i=3;i<argc;i++) {
            if(!strcmp(argv[i],"--backend") && i+1<argc) {
                if(!qn_gpu_backend_parse(argv[++i],&requested)) {
                    qn_diag_set_code(
                        &diag,
                        "QN-E7002",
                        0,
                        0,
                        "unknown GPU backend: %s",
                        argv[i]
                    );
                    return print_diag(QN_ERR_PARSE,&diag);
                }
            } else if(!strcmp(argv[i],"--receipt") && i+1<argc) {
                receipt_path=argv[++i];
            } else {
                qn_diag_set_code(
                    &diag,
                    "QN-E7002",
                    0,
                    0,
                    "unknown GPU option: %s",
                    argv[i]
                );
                return print_diag(QN_ERR_PARSE,&diag);
            }
        }

        if(is_compute) {
            QNGpuComputeProof proof;
            QNStatus status=qn_gpu_compute_proof(
                requested,
                &proof,
                &diag
            );
            if(status!=QN_OK) return print_diag(status,&diag);

            qn_gpu_compute_print(&proof,stdout);

            if(receipt_path) {
                status=qn_gpu_compute_write_receipt(
                    receipt_path,
                    &proof,
                    &diag
                );
                if(status!=QN_OK) return print_diag(status,&diag);
                printf("receipt=%s\n",receipt_path);
            }
            return 0;
        }

        QNGpuProbe probe;
        QNStatus status=qn_gpu_probe(&probe,&diag);
        if(status!=QN_OK) return print_diag(status,&diag);

        QNGpuDecision decision;
        status=qn_gpu_decide(
            requested,
            &probe,
            &decision,
            &diag
        );
        if(status!=QN_OK) return print_diag(status,&diag);

        qn_gpu_print_contract(&probe,&decision,stdout);

        if(receipt_path) {
            status=qn_gpu_write_receipt(
                receipt_path,
                &probe,
                &decision,
                &diag
            );
            if(status!=QN_OK) return print_diag(status,&diag);
            printf("receipt=%s\n",receipt_path);
        }

        return 0;
    }

    if(!strcmp(argv[1],"lex")) {
        size_t size=0; char *source=qn_read_text_file(argv[2],&size,&diag);
        if(!source) return print_diag(QN_ERR_IO,&diag);
        QNTokenList tokens={0}; QNStatus st=qn_lex(source,&tokens,&diag); free(source);
        if(st!=QN_OK) return print_diag(st,&diag);
        for(size_t i=0;i<tokens.count;i++) {
            const QNToken *t=&tokens.items[i];
            printf("%d:%d %-10s",t->line,t->column,qn_token_kind_name(t->kind));
            if(t->text[0]) printf(" %s",t->text);
            if(t->kind==TOK_INT) printf(" %llu",(unsigned long long)t->int_value);
            putchar('\n');
        }
        qn_tokens_free(&tokens); return 0;
    }




    if(!strcmp(argv[1],"approval")) {
        if(argc < 3) {
            usage(stderr);
            return QN_ERR_PARSE;
        }


        if(!strcmp(argv[2],"keygen-ed25519")) {
            const char *private_path = NULL;
            const char *public_path = NULL;

            for(int i=3;i<argc;i++) {
                if(!strcmp(argv[i],"--private") && i+1<argc) {
                    private_path=argv[++i];
                } else if(!strcmp(argv[i],"--public") && i+1<argc) {
                    public_path=argv[++i];
                } else {
                    fprintf(stderr,
                            "error[QN-E5004]: unknown keygen option '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            }

            if(!private_path || !public_path) {
                fprintf(stderr,
                        "error[QN-E5004]: keygen requires --private and --public\n");
                return QN_ERR_PARSE;
            }

            QNStatus status =
                qn_signed_approval_keypair_generate(
                    private_path,
                    public_path,
                    &diag
                );
            if(status!=QN_OK) return print_diag(status,&diag);

            uint8_t *public_key = qn_signed_approval_load_key(
                public_path,
                QN_ED25519_PUBLIC_KEY_BYTES,
                &diag
            );
            if(!public_key) return print_diag(QN_ERR_IO,&diag);

            uint8_t fingerprint[32];
            char fingerprint_hex[65];
            qn_ed25519_fingerprint(public_key,fingerprint);
            qn_hex32(fingerprint,fingerprint_hex);
            free(public_key);

            printf("QBIT_NOVA_ED25519_KEYGEN_V05\n");
            printf("private=%s\n",private_path);
            printf("public=%s\n",public_path);
            printf("issuer_fingerprint=%s\n",fingerprint_hex);
            printf("private_key_format=raw-32-byte-seed\n");
            printf("public_key_format=raw-32-byte\n");
            return 0;
        }

        if(!strcmp(argv[2],"derive-public-ed25519")) {
            const char *private_path = NULL;
            const char *public_path = NULL;

            for(int i=3;i<argc;i++) {
                if(!strcmp(argv[i],"--private") && i+1<argc) {
                    private_path=argv[++i];
                } else if(!strcmp(argv[i],"--public") && i+1<argc) {
                    public_path=argv[++i];
                } else {
                    fprintf(stderr,
                            "error[QN-E5004]: unknown derive option '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            }

            if(!private_path || !public_path) {
                fprintf(stderr,
                        "error[QN-E5004]: derive-public requires --private and --public\n");
                return QN_ERR_PARSE;
            }

            QNStatus status =
                qn_signed_approval_derive_public(
                    private_path,
                    public_path,
                    &diag
                );
            if(status!=QN_OK) return print_diag(status,&diag);

            printf("QBIT_NOVA_ED25519_PUBLIC_DERIVE_V05\n");
            printf("public=%s\n",public_path);
            return 0;
        }

        if(!strcmp(argv[2],"issue-ed25519")) {
            if(argc < 5) {
                usage(stderr);
                return QN_ERR_PARSE;
            }

            const char *source_path = argv[3];
            const char *capability_name = argv[4];
            const char *private_path = NULL;
            const char *output_path = "approval.qns";
            const char *nonce_hex = NULL;
            const char *context_text = "";
            uint64_t issued_at = current_unix_time();
            uint64_t expires_at = 0u;

            for(int i=5;i<argc;i++) {
                if(!strcmp(argv[i],"--private-key") && i+1<argc) {
                    private_path=argv[++i];
                } else if(!strcmp(argv[i],"-o") && i+1<argc) {
                    output_path=argv[++i];
                } else if(!strcmp(argv[i],"--issued-at") && i+1<argc) {
                    issued_at=parse_u64(argv[++i],"issued-at");
                } else if(!strcmp(argv[i],"--expires-at") && i+1<argc) {
                    expires_at=parse_u64(argv[++i],"expires-at");
                } else if(!strcmp(argv[i],"--nonce-hex") && i+1<argc) {
                    nonce_hex=argv[++i];
                } else if(!strcmp(argv[i],"--context") && i+1<argc) {
                    context_text=argv[++i];
                } else {
                    fprintf(stderr,
                            "error[QN-E5004]: unknown issue-ed25519 option '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            }

            if(!private_path || expires_at==0u) {
                fprintf(stderr,
                        "error[QN-E5004]: issue-ed25519 requires --private-key and --expires-at\n");
                return QN_ERR_PARSE;
            }

            QNCapabilityMask capability = 0u;
            if(!qn_capability_parse(capability_name,&capability)) {
                fprintf(stderr,
                        "error[QN-E5009]: unknown capability '%s'\n",
                        capability_name);
                return QN_ERR_PARSE;
            }

            QNBytecode bc={0};
            QNStatus status =
                compile_source(source_path,&bc,NULL,NULL,&diag);
            if(status!=QN_OK) return print_diag(status,&diag);

            uint8_t *private_key = qn_signed_approval_load_key(
                private_path,
                QN_ED25519_PRIVATE_KEY_BYTES,
                &diag
            );
            if(!private_key) {
                qn_bytecode_free(&bc);
                return print_diag(QN_ERR_IO,&diag);
            }

            uint8_t nonce[QN_SIGNED_APPROVAL_NONCE_BYTES];
            const uint8_t *nonce_pointer = NULL;
            if(nonce_hex) {
                if(!qn_signed_approval_parse_nonce_hex(
                        nonce_hex,
                        nonce)) {
                    qn_ed25519_wipe_private(private_key);
                    free(private_key);
                    qn_bytecode_free(&bc);
                    fprintf(stderr,
                            "error[QN-E5004]: nonce must be exactly 32 hex characters\n");
                    return QN_ERR_PARSE;
                }
                nonce_pointer=nonce;
            }

            QNSignedApprovalToken token;
            status=qn_signed_approval_issue(
                capability,
                bc.capability_mask,
                bc.source_digest,
                issued_at,
                expires_at,
                nonce_pointer,
                (const uint8_t *)context_text,
                strlen(context_text),
                private_key,
                &token,
                &diag
            );

            qn_ed25519_wipe_private(private_key);
            free(private_key);

            if(status==QN_OK) {
                status=qn_signed_approval_write(
                    output_path,
                    &token,
                    &diag
                );
            }

            if(status==QN_OK) {
                char source_hex[65];
                char issuer_hex[65];
                qn_hex32(bc.source_digest,source_hex);
                qn_hex32(token.issuer_fingerprint,issuer_hex);
                printf("QBIT_NOVA_ED25519_APPROVAL_ISSUE_V05\n");
                printf("output=%s\n",output_path);
                printf("capability=%s\n",
                       qn_capability_name(token.capability));
                printf("capability_id=0x%08x\n",
                       token.capability_id);
                printf("source_sha256=%s\n",source_hex);
                printf("issuer_fingerprint=%s\n",issuer_hex);
                printf("issued_at=%llu\n",
                       (unsigned long long)token.issued_at);
                printf("expires_at=%llu\n",
                       (unsigned long long)token.expires_at);
                printf("authentication=ed25519\n");
            }

            qn_bytecode_free(&bc);
            return status==QN_OK ? 0 : print_diag(status,&diag);
        }

        if(!strcmp(argv[2],"verify-ed25519")) {
            if(argc < 5) {
                usage(stderr);
                return QN_ERR_PARSE;
            }

            const char *source_path = argv[3];
            const char *token_path = argv[4];
            const char *public_path = NULL;
            const char *trust_store_path = NULL;
            uint64_t now = current_unix_time();

            for(int i=5;i<argc;i++) {
                if(!strcmp(argv[i],"--public-key") && i+1<argc) {
                    public_path=argv[++i];
                } else if(!strcmp(argv[i],"--trust-store-file") && i+1<argc) {
                    trust_store_path=argv[++i];
                } else if(!strcmp(argv[i],"--now") && i+1<argc) {
                    now=parse_u64(argv[++i],"now");
                } else {
                    fprintf(stderr,
                            "error[QN-E5004]: unknown verify-ed25519 option '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            }

            if((public_path == NULL) ==
               (trust_store_path == NULL)) {
                qn_diag_set_code(
                    &diag,
                    "QN-E5004",
                    0,
                    0,
                    "verify-ed25519 requires exactly one of "
                    "--public-key or --trust-store-file"
                );
                return print_diag(QN_ERR_RUNTIME,&diag);
            }

            QNBytecode bc={0};
            QNStatus status =
                compile_source(source_path,&bc,NULL,NULL,&diag);
            if(status!=QN_OK) return print_diag(status,&diag);

            QNSignedApprovalToken token;
            status=qn_signed_approval_read(
                token_path,
                &token,
                &diag
            );

            uint8_t public_key[QN_ED25519_PUBLIC_KEY_BYTES];

            if(status==QN_OK) {
                status=resolve_signed_approval_public_key(
                    public_path,
                    trust_store_path,
                    &token,
                    public_key,
                    &diag
                );
            }

            QNCapabilityMask approved = 0u;
            if(status==QN_OK) {
                status=qn_signed_approval_verify(
                    &token,
                    bc.source_digest,
                    bc.capability_mask,
                    now,
                    public_key,
                    &approved,
                    &diag
                );
            }

            if(status==QN_OK) {
                char token_hex[65];
                char issuer_hex[65];
                qn_hex32(token.token_digest,token_hex);
                qn_hex32(token.issuer_fingerprint,issuer_hex);
                printf("QBIT_NOVA_ED25519_APPROVAL_VERIFY_V05\n");
                printf("status=valid\n");
                printf(
                    "key_source=%s\n",
                    trust_store_path ?
                        "trust-store" :
                        "public-key"
                );
                printf("capability=%s\n",
                       qn_capability_name(approved));
                printf("issuer_fingerprint=%s\n",issuer_hex);
                printf("token_sha256=%s\n",token_hex);
            }

            qn_bytecode_free(&bc);
            return status==QN_OK ? 0 : print_diag(status,&diag);
        }

        if(!strcmp(argv[2],"issue")) {
            const char *source_path = argv[3];
            const char *capability_name = argv[4];
            const char *key_path = NULL;
            const char *output_path = "approval.qna";
            const char *nonce = NULL;
            uint64_t issued_at = current_unix_time();
            uint64_t expires_at = 0u;

            for(int i=5;i<argc;i++) {
                if(!strcmp(argv[i],"--key-file") && i+1<argc) {
                    key_path=argv[++i];
                } else if(!strcmp(argv[i],"-o") && i+1<argc) {
                    output_path=argv[++i];
                } else if(!strcmp(argv[i],"--issued-at") && i+1<argc) {
                    issued_at=parse_u64(argv[++i],"issued-at");
                } else if(!strcmp(argv[i],"--expires-at") && i+1<argc) {
                    expires_at=parse_u64(argv[++i],"expires-at");
                } else if(!strcmp(argv[i],"--nonce") && i+1<argc) {
                    nonce=argv[++i];
                } else {
                    fprintf(stderr,
                            "error[QN-E-APPROVAL-012]: unknown issue option '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            }

            if(!key_path || expires_at == 0u) {
                fprintf(stderr,
                        "error[QN-E-APPROVAL-012]: issue requires --key-file and --expires-at\n");
                return QN_ERR_PARSE;
            }

            QNCapabilityMask capability = 0;
            if(!qn_capability_parse(capability_name,&capability)) {
                fprintf(stderr,
                        "error[QN-E-APPROVAL-012]: unknown capability '%s'\n",
                        capability_name);
                return QN_ERR_PARSE;
            }

            QNBytecode bc={0};
            QNStatus status =
                compile_source(source_path,&bc,NULL,NULL,&diag);
            if(status!=QN_OK) return print_diag(status,&diag);

            size_t key_size = 0;
            uint8_t *key =
                qn_approval_load_key(key_path,&key_size,&diag);
            if(!key) {
                qn_bytecode_free(&bc);
                return print_diag(QN_ERR_RUNTIME,&diag);
            }

            QNApprovalToken token;
            status=qn_approval_create(
                capability,
                bc.source_digest,
                issued_at,
                expires_at,
                nonce,
                key,
                key_size,
                &token,
                &diag
            );
            free(key);

            if(status==QN_OK) {
                status=qn_approval_write(
                    output_path,
                    &token,
                    &diag
                );
            }

            if(status==QN_OK) {
                char source_hex[65];
                qn_hex32(bc.source_digest,source_hex);
                printf("QBIT_NOVA_APPROVAL_ISSUE_V04\n");
                printf("output=%s\n",output_path);
                printf("capability=%s\n",token.capability_name);
                printf("source_sha256=%s\n",source_hex);
                printf("issued_at=%llu\n",
                       (unsigned long long)token.issued_at);
                printf("expires_at=%llu\n",
                       (unsigned long long)token.expires_at);
                printf("authentication=hmac-sha256\n");
            }

            qn_bytecode_free(&bc);
            return status==QN_OK ? 0 : print_diag(status,&diag);
        }

        if(!strcmp(argv[2],"verify")) {
            const char *source_path = argv[3];
            const char *token_path = argv[4];
            const char *key_path = NULL;
            uint64_t now = current_unix_time();

            for(int i=5;i<argc;i++) {
                if(!strcmp(argv[i],"--key-file") && i+1<argc) {
                    key_path=argv[++i];
                } else if(!strcmp(argv[i],"--now") && i+1<argc) {
                    now=parse_u64(argv[++i],"now");
                } else {
                    fprintf(stderr,
                            "error[QN-E-APPROVAL-012]: unknown verify option '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            }

            if(!key_path) {
                fprintf(stderr,
                        "error[QN-E-APPROVAL-012]: verify requires --key-file\n");
                return QN_ERR_PARSE;
            }

            QNBytecode bc={0};
            QNStatus status =
                compile_source(source_path,&bc,NULL,NULL,&diag);
            if(status!=QN_OK) return print_diag(status,&diag);

            QNApprovalToken token;
            status=qn_approval_read(token_path,&token,&diag);

            size_t key_size = 0;
            uint8_t *key = NULL;
            if(status==QN_OK) {
                key=qn_approval_load_key(
                    key_path,
                    &key_size,
                    &diag
                );
                if(!key) status=QN_ERR_RUNTIME;
            }

            QNCapabilityMask approved = 0;
            if(status==QN_OK) {
                status=qn_approval_verify(
                    &token,
                    bc.source_digest,
                    now,
                    key,
                    key_size,
                    &approved,
                    &diag
                );
            }
            free(key);

            if(status==QN_OK) {
                char token_hex[65];
                qn_hex32(token.token_digest,token_hex);
                printf("QBIT_NOVA_APPROVAL_VERIFY_V04\n");
                printf("status=valid\n");
                printf("capability=%s\n",
                       qn_capability_name(approved));
                printf("token_sha256=%s\n",token_hex);
            }

            qn_bytecode_free(&bc);
            return status==QN_OK ? 0 : print_diag(status,&diag);
        }

        fprintf(stderr,
                "error[QN-E-APPROVAL-012]: expected issue or verify\n");
        return QN_ERR_PARSE;
    }

    if(!strcmp(argv[1],"guard")) {
        QNCapabilityMask requested = 0;
        if(!qn_capability_parse(argv[2],&requested)) {
            fprintf(stderr,"error[QN-E-GUARD-002]: unknown capability name '%s'\n",
                    argv[2]);
            return QN_ERR_PARSE;
        }

        QNGuardPolicy policy;
        qn_guard_policy_safe(&policy);

        for(int i=3;i<argc;i++) {
            if(!strcmp(argv[i],"--approve") && i+1<argc) {
                QNCapabilityMask approved = 0;
                const char *name = argv[++i];
                if(!qn_capability_parse(name,&approved)) {
                    fprintf(stderr,
                            "error[QN-E-GUARD-002]: unknown capability name '%s'\n",
                            name);
                    return QN_ERR_PARSE;
                }
                policy.approved |= approved;
            } else if(!strcmp(argv[i],"--policy") && i+1<argc) {
                if(!qn_guard_policy_select(argv[++i],&policy)) {
                    fprintf(stderr,"error[QN-E-GUARD-003]: unknown policy '%s'\n",
                            argv[i]);
                    return QN_ERR_PARSE;
                }
            } else {
                fprintf(stderr,"error[QN-E-GUARD-004]: unknown guard option '%s'\n",
                        argv[i]);
                return QN_ERR_PARSE;
            }
        }

        QNGuardDecision decision =
            qn_guard_evaluate(requested,&policy);

        char requested_text[256];
        char missing_text[256];
        char blocked_text[256];
        qn_capability_format(
            decision.requested,
            requested_text,
            sizeof(requested_text)
        );
        qn_capability_format(
            decision.missing_approval,
            missing_text,
            sizeof(missing_text)
        );
        qn_capability_format(
            decision.blocked,
            blocked_text,
            sizeof(blocked_text)
        );

        printf("QBIT_NOVA_CAPABILITY_GUARD_V03\n");
        printf("profile=%s\n",policy.profile);
        printf("requested=%s\n",requested_text);
        printf("decision=%s\n",qn_guard_status_name(decision.status));
        printf("missing_approval=%s\n",missing_text);
        printf("blocked=%s\n",blocked_text);
        printf("reason=%s\n",decision.reason);

        return decision.status == QN_GUARD_ALLOWED ? 0 :
               decision.status == QN_GUARD_NEEDS_APPROVAL ? 10 : 11;
    }

    if(!strcmp(argv[1],"qir")) {
        QNProgram program = {0};
        uint8_t digest[32];
        QNStatus st =
            parse_source_program(argv[2], &program, digest, &diag);
        if(st != QN_OK) return print_diag(st, &diag);

        QNQIRProgram qir = {0};
        st = qn_qir_build(&program, digest, &qir, &diag);
        qn_program_free(&program);
        if(st != QN_OK) return print_diag(st, &diag);

        qn_qir_dump(&qir, stdout);
        qn_qir_free(&qir);
        return 0;
    }

    if(!strcmp(argv[1],"check")) {
        QNBytecode bc={0}; QNStatus st=compile_source(argv[2],&bc,NULL,NULL,&diag);
        if(st!=QN_OK) return print_diag(st,&diag);
        printf("PASS: QBIT_NOVA_NATIVE_CHECK_V01\nqubits=%u\ninstructions=%zu\n",
               bc.total_qubits,bc.instruction_count);
        qn_bytecode_free(&bc); return 0;
    }

    if(!strcmp(argv[1],"build")) {
        const char *out_path=NULL;

        for(int i=3;i<argc;i++) {
            if(!strcmp(argv[i],"-o") && i+1<argc) {
                out_path=argv[++i];
            } else {
                fprintf(
                    stderr,
                    "unknown option: %s\n",
                    argv[i]
                );
                return QN_ERR_PARSE;
            }
        }

        if(!out_path) {
            fprintf(
                stderr,
                "build requires -o <file.qbc>\n"
            );
            return QN_ERR_PARSE;
        }

        QNBytecode bc={0};
        QNProgram program={0};

        /*
         * Keep the frontend-valid AST alive for the BUILD-only
         * pre-V10 serialization boundary check.
         *
         * CHECK continues to use compile_source() normally and
         * therefore remains a frontend/QIR validation operation.
         */
        QNStatus st=compile_source(
            argv[2],
            &bc,
            NULL,
            &program,
            &diag
        );

        if(st!=QN_OK) {
            return print_diag(
                st,
                &diag
            );
        }

        /*
         * STEP9_PRE_V10_TENSOR_BUILD_FAIL_CLOSED
         *
         * Tensor declarations are now valid frontend/QIR syntax.
         *
         * They MUST NOT be serialized into the GPIO-only QBC v10 contract or
         * any frozen QBC v1-v9 contract.
         *
         * Tensor QBC serialization will be enabled only after the complete
         * native data/media/module/capability ABI release gate exists.
         *
         * This guard deliberately lives in BUILD, immediately
         * before QBC serialization.
         *
         * Therefore:
         *
         *   qnova check tensor.qn
         *       -> frontend-valid
         *
         *   qnova build tensor.qn
         *       -> QN-E7710 fail-closed
         *
         * No legacy QBC is emitted for tensor-bearing programs.
         */
        for(size_t i=0u;i<program.count;++i) {
            const QNStmt *stmt=&program.items[i];

            if(stmt->kind==STMT_TENSOR_DECL) {
                qn_diag_set_code(
                    &diag,
                    "QN-E7710",
                    stmt->line,
                    stmt->column,
                    "native tensor program is frontend-valid but tensor "
                    "QBC serialization is intentionally not released yet"
                );

                qn_program_free(&program);
                qn_bytecode_free(&bc);

                return print_diag(
                    QN_ERR_QBC,
                    &diag
                );
            }
        }

        /*
         * AST is no longer needed after the pre-serialization
         * policy gate has passed.
         */
        qn_program_free(&program);

        uint8_t *data=NULL;
        size_t n=0;

        st=qn_qbc_encode(
            &bc,
            &data,
            &n,
            &diag
        );

        if(
            st==QN_OK &&
            !qn_write_binary_file_atomic(
                out_path,
                data,
                n,
                &diag
            )
        ) {
            st=QN_ERR_IO;
        }

        if(st==QN_OK) {
            uint8_t d[32];
            char hex[65];

            qn_sha256(
                data,
                n,
                d
            );

            qn_hex32(
                d,
                hex
            );

            printf(
                "QBIT_NOVA_QBC_BUILD_V01\n"
                "output=%s\n"
                "bytes=%zu\n"
                "sha256=%s\n",
                out_path,
                n,
                hex
            );
        }

        free(data);
        qn_bytecode_free(&bc);

        return st==QN_OK
            ? 0
            : print_diag(
                st,
                &diag
            );
    }

    if(!strcmp(argv[1],"run") || !strcmp(argv[1],"exec")) {
        RunOptions options;
        parse_run_opts(argc,argv,3,&options);
        QNBytecode bc={0}; QNStatus st;
        if(!strcmp(argv[1],"run")) {
            st=compile_source(argv[2],&bc,NULL,NULL,&diag);
        } else {
            size_t n=0; uint8_t *data=qn_read_binary_file(argv[2],&n,&diag);
            if(!data) return print_diag(QN_ERR_IO,&diag);
            st=qn_qbc_decode(data,n,&bc,&diag); free(data);
        }
        if(st!=QN_OK) return print_diag(st,&diag);

        QNRuntimeInputs runtime_inputs;
        st=resolve_runtime_inputs(&bc,&options,&runtime_inputs,&diag);
        if(st!=QN_OK) {
            qn_bytecode_free(&bc);
            return print_diag(st,&diag);
        }

        st=apply_approval_to_policy(
            &options,
            &bc,
            &options.policy,
            &diag
        );
        if(st!=QN_OK) {
            qn_bytecode_free(&bc);
            return print_diag(st,&diag);
        }

        bool revocation_checked=false;

        st=check_revocation_before_replay(
            &options,
            &options.policy,
            &diag
        );

        if(st!=QN_OK) {
            qn_bytecode_free(&bc);
            return print_diag(st,&diag);
        }

        if(options.signed_approval_file) {
            revocation_checked=true;
        }

        st=preflight_before_replay_consume(
            &bc,
            options.shots,
            options.shots_explicit,
            options.seed_explicit,
            &options.policy,
            &options.device,
            options.device_backend_explicit,
            options.device_hold_explicit,
            &diag
        );

        if(st!=QN_OK) {
            qn_bytecode_free(&bc);
            return print_diag(st,&diag);
        }

        QNGpuQvmRoute qvm_route;

        st=qn_gpu_route_qvm(
            options.backend,
            options.backend_explicit,
            &bc,
            &qvm_route,
            &diag
        );

        if(st!=QN_OK) {
            qn_bytecode_free(&bc);
            return print_diag(st,&diag);
        }

        bool replay_consumed=false;

        if(options.signed_approval_file) {
            st=qn_replay_ledger_consume(
                options.replay_ledger_file,
                options.policy.approval_digest,
                &replay_consumed,
                &diag
            );

            if(st!=QN_OK || !replay_consumed) {
                if(st==QN_OK) {
                    qn_diag_set_code(
                        &diag,
                        "QN-E5202",
                        0,
                        0,
                        "replay ledger did not consume token"
                    );
                    st=QN_ERR_IO;
                }

                qn_bytecode_free(&bc);
                return print_diag(st,&diag);
            }
        }

        QNRunResult result={0};
        st=qn_vm_run_guarded_with_device(
            &bc,
            options.shots,
            options.seed,
            &options.policy,
            &qvm_route,
            runtime_inputs.provided ? &runtime_inputs : NULL,
            &options.device,
            &result,
            &diag
        );
        if(st==QN_OK) {
            result.approval_revocation_checked =
                revocation_checked;
            result.approval_token_revoked = false;
            result.approval_issuer_revoked = false;
            result.approval_replay_consumed =
                replay_consumed;

            qn_print_result(&bc,&result,stdout);
        }

        if(st==QN_OK && options.receipt) {
            st=qn_write_receipt(
                options.receipt,
                &bc,
                &result,
                &diag
            );
            if(st==QN_OK) printf("receipt=%s\n",options.receipt);
        }
        qn_run_result_free(&result); qn_bytecode_free(&bc);
        return st==QN_OK?0:print_diag(st,&diag);
    }

    usage(stderr);
    return 1;
}
