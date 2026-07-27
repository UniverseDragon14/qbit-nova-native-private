#include "qn_lexer.h"
#include "qn_parser.h"
#include "qn_qbc.h"
#include "qn_qir.h"
#include "qn_vm.h"
#include "qn_approval.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *f) {
    fprintf(f,
        "QBIT NOVA Native v0.4.0\n"
        "Usage:\n"
        "  qnova lex <file.qn>\n"
        "  qnova check <file.qn>\n"
        "  qnova qir <file.qn>\n"
        "  qnova guard <capability> [--approve <capability>]\n"
        "  qnova approval issue <file.qn> <capability> --key-file KEY\n"
        "            --expires-at UNIX [-o token.qna] [--issued-at UNIX]\n"
        "            [--nonce TEXT]\n"
        "  qnova approval verify <file.qn> <token.qna> --key-file KEY\n"
        "            [--now UNIX]\n"
        "  qnova build <file.qn> -o <file.qbc>\n"
        "  qnova run <file.qn> [--shots N] [--seed N] [--policy safe|deny-all]\n"
        "            [--approval-file token.qna --approval-key-file KEY]\n"
        "            [--now UNIX] [--receipt file.json]\n"
        "  qnova exec <file.qbc> [--shots N] [--seed N] [--policy safe|deny-all]\n"
        "            [--approval-file token.qna --approval-key-file KEY]\n"
        "            [--now UNIX] [--receipt file.json]\n"
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
    const char *receipt;
    const char *approval_file;
    const char *approval_key_file;
    uint64_t approval_now;
    bool has_approval_now;
    QNGuardPolicy policy;
} RunOptions;

static void parse_run_opts(int argc,
                           char **argv,
                           int from,
                           RunOptions *options) {
    memset(options, 0, sizeof(*options));
    qn_guard_policy_safe(&options->policy);

    for(int i=from;i<argc;i++) {
        if(!strcmp(argv[i],"--shots") && i+1<argc) {
            options->shots=(uint32_t)parse_u64(argv[++i],"shots");
        } else if(!strcmp(argv[i],"--seed") && i+1<argc) {
            options->seed=parse_u64(argv[++i],"seed");
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
        } else if(!strcmp(argv[i],"--now") && i+1<argc) {
            options->approval_now=parse_u64(argv[++i],"now");
            options->has_approval_now=true;
        } else {
            fprintf(stderr,"unknown option: %s\n",argv[i]);
            exit(QN_ERR_PARSE);
        }
    }
}


static uint64_t current_unix_time(void) {
    time_t now = time(NULL);
    if (now < 0) {
        fprintf(stderr,"cannot read system time\n");
        exit(QN_ERR_RUNTIME);
    }
    return (uint64_t)now;
}

static QNStatus apply_approval_to_policy(
    const RunOptions *options,
    const QNBytecode *bc,
    QNGuardPolicy *policy,
    QNDiagnostic *diag
) {
    bool has_file = options->approval_file != NULL;
    bool has_key = options->approval_key_file != NULL;

    if (has_file != has_key) {
        qn_diag_set_code(
            diag,
            "QN-E-APPROVAL-011",
            0,
            0,
            "--approval-file and --approval-key-file must be used together"
        );
        return QN_ERR_RUNTIME;
    }

    if (!has_file) return QN_OK;

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

    uint64_t now = options->has_approval_now
        ? options->approval_now
        : current_unix_time();

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
    return QN_OK;
}

int main(int argc,char **argv) {
    if(argc<2){ usage(stderr); return 1; }
    if(!strcmp(argv[1],"version")){
        printf("QBIT NOVA Native %d.%d.%d\n",QN_VERSION_MAJOR,QN_VERSION_MINOR,QN_VERSION_PATCH);
        printf("runtime=C17\npython_dependency=false\nboundary=software_virtual_qcpu\n");
        return 0;
    }
    if(argc<3){ usage(stderr); return 1; }

    QNDiagnostic diag={0};

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
        if(argc < 5) {
            usage(stderr);
            return QN_ERR_PARSE;
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
            if(!strcmp(argv[i],"-o") && i+1<argc) out_path=argv[++i];
            else { fprintf(stderr,"unknown option: %s\n",argv[i]); return QN_ERR_PARSE; }
        }
        if(!out_path){ fprintf(stderr,"build requires -o <file.qbc>\n"); return QN_ERR_PARSE; }
        QNBytecode bc={0}; QNStatus st=compile_source(argv[2],&bc,NULL,NULL,&diag);
        if(st!=QN_OK) return print_diag(st,&diag);
        uint8_t *data=NULL; size_t n=0;
        st=qn_qbc_encode(&bc,&data,&n,&diag);
        if(st==QN_OK && !qn_write_binary_file(out_path,data,n,&diag)) st=QN_ERR_IO;
        if(st==QN_OK){
            uint8_t d[32]; char hex[65]; qn_sha256(data,n,d); qn_hex32(d,hex);
            printf("QBIT_NOVA_QBC_BUILD_V01\noutput=%s\nbytes=%zu\nsha256=%s\n",out_path,n,hex);
        }
        free(data); qn_bytecode_free(&bc);
        return st==QN_OK?0:print_diag(st,&diag);
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

        QNRunResult result={0};
        st=qn_vm_run_guarded(
            &bc,
            options.shots,
            options.seed,
            &options.policy,
            &result,
            &diag
        );
        if(st==QN_OK) qn_print_result(&bc,&result,stdout);
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
