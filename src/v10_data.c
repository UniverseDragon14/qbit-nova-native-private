#include "qn_v10_data.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    T_EOF = 0,
    T_NL,
    T_LET,
    T_IDENT,
    T_COLON,
    T_EQ,
    T_NUM,
    T_STR,
    T_BYTES
} TK;

typedef struct {
    TK k;
    int line;
    int col;
    char text[QN_NAME_CAP];
    float f;
    uint8_t *p;
    uint32_t n;
} Tok;

typedef struct {
    const char *s;
    size_t i;
    int line;
    int col;
    Tok hold;
    bool has_hold;
    QNDiagnostic *d;
} Lex;

static void de(QNDiagnostic *d, const char *code, int line, int column,
               const char *message) {
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->line = line;
    d->column = column;
    if (code) snprintf(d->code, sizeof(d->code), "%s", code);
    if (message) snprintf(d->message, sizeof(d->message), "%s", message);
}

static void tf(Tok *t) {
    if (!t) return;
    free(t->p);
    memset(t, 0, sizeof(*t));
}

static bool ascii_digit(unsigned char c) {
    return c >= (unsigned char)'0' && c <= (unsigned char)'9';
}

static bool ascii_alpha(unsigned char c) {
    return (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
           (c >= (unsigned char)'A' && c <= (unsigned char)'Z');
}

static bool ascii_alnum(unsigned char c) {
    return ascii_alpha(c) || ascii_digit(c);
}

static int hx(unsigned char c) {
    if (c >= (unsigned char)'0' && c <= (unsigned char)'9') return c - '0';
    if (c >= (unsigned char)'a' && c <= (unsigned char)'f') return c - 'a' + 10;
    if (c >= (unsigned char)'A' && c <= (unsigned char)'F') return c - 'A' + 10;
    return -1;
}

static bool ab(uint8_t **p, uint32_t *n, uint32_t *cap, uint8_t v,
               uint32_t limit, QNDiagnostic *d, int line, int column) {
    if (*n >= limit) {
        de(d, "QN-E7811", line, column, "V10 literal exceeds bounded byte limit");
        return false;
    }

    if (*n == *cap) {
        uint32_t next = *cap ? *cap * 2u : 64u;
        if (next > limit) next = limit;
        if (next <= *cap) {
            de(d, "QN-E7811", line, column, "V10 literal capacity overflow");
            return false;
        }
        uint8_t *grown = (uint8_t *)realloc(*p, next);
        if (!grown) {
            de(d, "QN-E7811", line, column, "out of memory reading V10 literal");
            return false;
        }
        *p = grown;
        *cap = next;
    }

    (*p)[(*n)++] = v;
    return true;
}

static bool quoted(Lex *l, Tok *t, bool bytes) {
    int start_column = l->col;
    if (bytes) {
        ++l->i;
        ++l->col;
    }
    if (l->s[l->i] != '"') return false;

    ++l->i;
    ++l->col;
    uint8_t *p = NULL;
    uint32_t n = 0u;
    uint32_t cap = 0u;
    uint32_t limit = bytes ? QN_MAX_BYTES_BUFFER : QN_MAX_STRING_BYTES;

    while (l->s[l->i] && l->s[l->i] != '"') {
        unsigned char c = (unsigned char)l->s[l->i];
        if (c == '\n' || c == '\r') {
            free(p);
            de(l->d, "QN-E7811", t->line, start_column,
               "newline inside V10 quoted literal");
            return false;
        }

        if (c == '\\') {
            ++l->i;
            ++l->col;
            unsigned char e = (unsigned char)l->s[l->i];
            uint8_t v = 0u;
            if (!e) {
                free(p);
                de(l->d, "QN-E7811", t->line, start_column,
                   "unterminated V10 escape");
                return false;
            }

            if (e == 'n') v = '\n';
            else if (e == 'r') v = '\r';
            else if (e == 't') v = '\t';
            else if (e == '\\') v = '\\';
            else if (e == '"') v = '"';
            else if (e == 'x' && bytes) {
                int a = hx((unsigned char)l->s[l->i + 1u]);
                int b = hx((unsigned char)l->s[l->i + 2u]);
                if (a < 0 || b < 0) {
                    free(p);
                    de(l->d, "QN-E7811", t->line, l->col,
                       "bytes \\x escape requires two hex digits");
                    return false;
                }
                v = (uint8_t)((a << 4) | b);
                l->i += 3u;
                l->col += 3;
                if (!ab(&p, &n, &cap, v, limit, l->d, t->line, start_column)) {
                    free(p);
                    return false;
                }
                continue;
            } else {
                free(p);
                de(l->d, "QN-E7811", t->line, l->col,
                   "unsupported V10 literal escape");
                return false;
            }

            ++l->i;
            ++l->col;
            if (!ab(&p, &n, &cap, v, limit, l->d, t->line, start_column)) {
                free(p);
                return false;
            }
            continue;
        }

        ++l->i;
        ++l->col;
        if (!ab(&p, &n, &cap, c, limit, l->d, t->line, start_column)) {
            free(p);
            return false;
        }
    }

    if (l->s[l->i] != '"') {
        free(p);
        de(l->d, "QN-E7811", t->line, start_column,
           "unterminated V10 quoted literal");
        return false;
    }

    ++l->i;
    ++l->col;
    t->k = bytes ? T_BYTES : T_STR;
    t->p = p;
    t->n = n;
    return true;
}

static QNStatus next0(Lex *l, Tok *t) {
    memset(t, 0, sizeof(*t));

    for (;;) {
        unsigned char c = (unsigned char)l->s[l->i];
        if (!c) {
            t->k = T_EOF;
            t->line = l->line;
            t->col = l->col;
            return QN_OK;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++l->i;
            ++l->col;
            continue;
        }
        if (c == '#' || (c == '/' && l->s[l->i + 1u] == '/')) {
            if (c == '/') {
                l->i += 2u;
                l->col += 2;
            }
            while (l->s[l->i] && l->s[l->i] != '\n') {
                ++l->i;
                ++l->col;
            }
            continue;
        }
        break;
    }

    unsigned char c = (unsigned char)l->s[l->i];
    t->line = l->line;
    t->col = l->col;

    if (c == '\n' || c == ';') {
        t->k = T_NL;
        ++l->i;
        if (c == '\n') {
            ++l->line;
            l->col = 1;
        } else {
            ++l->col;
        }
        return QN_OK;
    }
    if (c == ':') {
        t->k = T_COLON;
        ++l->i;
        ++l->col;
        return QN_OK;
    }
    if (c == '=') {
        t->k = T_EQ;
        ++l->i;
        ++l->col;
        return QN_OK;
    }
    if (c == '"') return quoted(l, t, false) ? QN_OK : QN_ERR_LEX;
    if (c == 'b' && l->s[l->i + 1u] == '"') {
        return quoted(l, t, true) ? QN_OK : QN_ERR_LEX;
    }

    if (ascii_digit(c) || (c == '-' && ascii_digit((unsigned char)l->s[l->i + 1u]))) {
        size_t start = l->i;
        int start_column = l->col;
        if (l->s[l->i] == '-') {
            ++l->i;
            ++l->col;
        }
        while (ascii_digit((unsigned char)l->s[l->i])) {
            ++l->i;
            ++l->col;
        }
        if (l->s[l->i] == '.') {
            ++l->i;
            ++l->col;
            if (!ascii_digit((unsigned char)l->s[l->i])) {
                de(l->d, "QN-E7811", t->line, start_column,
                   "f32 decimal point requires digits");
                return QN_ERR_LEX;
            }
            while (ascii_digit((unsigned char)l->s[l->i])) {
                ++l->i;
                ++l->col;
            }
        }
        if (l->s[l->i] == 'e' || l->s[l->i] == 'E') {
            ++l->i;
            ++l->col;
            if (l->s[l->i] == '+' || l->s[l->i] == '-') {
                ++l->i;
                ++l->col;
            }
            if (!ascii_digit((unsigned char)l->s[l->i])) {
                de(l->d, "QN-E7811", t->line, start_column,
                   "f32 exponent requires digits");
                return QN_ERR_LEX;
            }
            while (ascii_digit((unsigned char)l->s[l->i])) {
                ++l->i;
                ++l->col;
            }
        }

        size_t n = l->i - start;
        if (n >= 128u) {
            de(l->d, "QN-E7811", t->line, start_column, "f32 literal too long");
            return QN_ERR_LEX;
        }
        char buffer[128];
        memcpy(buffer, l->s + start, n);
        buffer[n] = '\0';
        errno = 0;
        char *end = NULL;
        float value = strtof(buffer, &end);
        if (errno == ERANGE || !end || *end || !isfinite(value)) {
            de(l->d, "QN-E7811", t->line, start_column,
               "invalid or non-finite f32 literal");
            return QN_ERR_LEX;
        }
        t->k = T_NUM;
        t->f = value;
        return QN_OK;
    }

    if (ascii_alpha(c) || c == '_') {
        size_t start = l->i;
        while (ascii_alnum((unsigned char)l->s[l->i]) || l->s[l->i] == '_') {
            ++l->i;
            ++l->col;
        }
        size_t n = l->i - start;
        if (n >= sizeof(t->text)) {
            de(l->d, "QN-E7811", t->line, t->col, "V10 identifier too long");
            return QN_ERR_LEX;
        }
        memcpy(t->text, l->s + start, n);
        t->text[n] = '\0';
        t->k = strcmp(t->text, "let") == 0 ? T_LET : T_IDENT;
        return QN_OK;
    }

    de(l->d, "QN-E7811", t->line, t->col,
       "unexpected character in V10 native-data source");
    return QN_ERR_LEX;
}

static QNStatus nx(Lex *l, Tok *t) {
    if (l->has_hold) {
        *t = l->hold;
        memset(&l->hold, 0, sizeof(l->hold));
        l->has_hold = false;
        return QN_OK;
    }
    return next0(l, t);
}

static QNStatus ex(Lex *l, TK expected, Tok *t, const char *message) {
    QNStatus status = nx(l, t);
    if (status != QN_OK) return status;
    if (t->k != expected) {
        de(l->d, "QN-E7812", t->line, t->col, message);
        tf(t);
        return QN_ERR_PARSE;
    }
    return QN_OK;
}

static bool exists(const QNV10DataProgram *program, const char *name) {
    for (uint16_t i = 0u; i < program->count; ++i) {
        if (strcmp(program->declarations[i].name, name) == 0) return true;
    }
    return false;
}

static QNStatus validate_decl(QNV10DataDecl *decl, QNDiagnostic *diag) {
    if (decl->kind == QN_VALUE_F32) return qn_f32_validate(decl->as.f32, diag);
    if (decl->kind == QN_VALUE_STRING) {
        QNStringView view = {(const char *)decl->as.blob.data, decl->as.blob.byte_length};
        return qn_string_validate(view, QN_MAX_STRING_BYTES, true, diag);
    }
    if (decl->kind == QN_VALUE_BYTES) {
        QNBytesView view = {decl->as.blob.data, decl->as.blob.byte_length};
        return qn_bytes_validate(view, QN_MAX_BYTES_BUFFER, diag);
    }
    de(diag, "QN-E7815", decl->line, decl->column, "unsupported V10 AST kind");
    return QN_ERR_SEMANTIC;
}

QNStatus qn_v10_data_parse_source(const char *source,
                                  QNV10DataProgram *out,
                                  QNDiagnostic *diag) {
    if (!source || !out || !diag) return QN_ERR_RUNTIME;
    memset(out, 0, sizeof(*out));

    size_t source_bytes = strlen(source);
    if (source_bytes > QN_MAX_SOURCE_BYTES) {
        de(diag, "QN-E7810", 1, 1, "V10 source exceeds source byte limit");
        return QN_ERR_LIMIT;
    }

    Lex lexer = {.s = source, .line = 1, .col = 1, .d = diag};
    QNStatus status = QN_OK;
    Tok a = {0}, name = {0}, colon = {0}, type = {0}, eq = {0}, value = {0}, end = {0};

    for (;;) {
        status = nx(&lexer, &a);
        if (status != QN_OK) goto fail;
        while (a.k == T_NL) {
            tf(&a);
            status = nx(&lexer, &a);
            if (status != QN_OK) goto fail;
        }
        if (a.k == T_EOF) {
            tf(&a);
            break;
        }
        if (a.k != T_LET) {
            de(diag, "QN-E7812", a.line, a.col, "expected 'let' in V10 declaration");
            status = QN_ERR_PARSE;
            goto fail;
        }
        if (out->count >= QN_V10_MAX_DECLS) {
            de(diag, "QN-E7812", a.line, a.col, "V10 declaration limit exceeded");
            status = QN_ERR_LIMIT;
            goto fail;
        }
        if ((status = ex(&lexer, T_IDENT, &name, "expected V10 declaration name")) != QN_OK) goto fail;
        if ((status = ex(&lexer, T_COLON, &colon, "expected ':' after V10 declaration name")) != QN_OK) goto fail;
        if ((status = ex(&lexer, T_IDENT, &type, "expected V10 native-data type")) != QN_OK) goto fail;
        if ((status = ex(&lexer, T_EQ, &eq, "expected '=' before V10 literal")) != QN_OK) goto fail;
        if ((status = nx(&lexer, &value)) != QN_OK) goto fail;

        if (exists(out, name.text)) {
            de(diag, "QN-E7813", name.line, name.col,
               "duplicate V10 native-data declaration");
            status = QN_ERR_SEMANTIC;
            goto fail;
        }

        QNV10DataDecl *decl = &out->declarations[out->count];
        memset(decl, 0, sizeof(*decl));
        decl->line = a.line;
        decl->column = a.col;
        snprintf(decl->name, sizeof(decl->name), "%s", name.text);

        if (strcmp(type.text, "f32") == 0) {
            if (value.k != T_NUM) {
                de(diag, "QN-E7814", value.line, value.col, "f32 requires an f32 literal");
                status = QN_ERR_PARSE;
                goto fail;
            }
            decl->kind = QN_VALUE_F32;
            decl->as.f32 = value.f;
        } else if (strcmp(type.text, "string") == 0) {
            if (value.k != T_STR) {
                de(diag, "QN-E7814", value.line, value.col,
                   "string requires a quoted UTF-8 literal");
                status = QN_ERR_PARSE;
                goto fail;
            }
            decl->kind = QN_VALUE_STRING;
            decl->as.blob.data = value.p;
            decl->as.blob.byte_length = value.n;
            value.p = NULL;
        } else if (strcmp(type.text, "bytes") == 0) {
            if (value.k != T_BYTES) {
                de(diag, "QN-E7814", value.line, value.col,
                   "bytes requires a b\"...\" literal");
                status = QN_ERR_PARSE;
                goto fail;
            }
            decl->kind = QN_VALUE_BYTES;
            decl->as.blob.data = value.p;
            decl->as.blob.byte_length = value.n;
            value.p = NULL;
        } else {
            de(diag, "QN-E7813", type.line, type.col,
               "V10 Step2 type must be f32, string, or bytes");
            status = QN_ERR_PARSE;
            goto fail;
        }

        ++out->count;
        status = validate_decl(decl, diag);
        if (status != QN_OK) goto fail;

        if ((status = nx(&lexer, &end)) != QN_OK) goto fail;
        if (end.k != T_NL && end.k != T_EOF) {
            de(diag, "QN-E7812", end.line, end.col,
               "expected end of line after V10 declaration");
            status = QN_ERR_PARSE;
            goto fail;
        }
        if (end.k == T_EOF) {
            tf(&end);
            break;
        }

        tf(&a);
        tf(&name);
        tf(&colon);
        tf(&type);
        tf(&eq);
        tf(&value);
        tf(&end);
    }

    if (!out->count) {
        de(diag, "QN-E7812", 1, 1, "V10 native-data program contains no declarations");
        status = QN_ERR_PARSE;
        goto fail;
    }

    tf(&a);
    tf(&name);
    tf(&colon);
    tf(&type);
    tf(&eq);
    tf(&value);
    tf(&end);
    tf(&lexer.hold);
    return QN_OK;

fail:
    tf(&a);
    tf(&name);
    tf(&colon);
    tf(&type);
    tf(&eq);
    tf(&value);
    tf(&end);
    tf(&lexer.hold);
    qn_v10_data_program_free(out);
    return status;
}

void qn_v10_data_program_free(QNV10DataProgram *program) {
    if (!program) return;
    for (uint16_t i = 0u; i < program->count; ++i) {
        if (program->declarations[i].kind == QN_VALUE_STRING ||
            program->declarations[i].kind == QN_VALUE_BYTES) {
            free(program->declarations[i].as.blob.data);
        }
    }
    memset(program, 0, sizeof(*program));
}

QNStatus qn_v10_data_qir_build(const QNV10DataProgram *program,
                               QNV10DataQIRProgram *qir,
                               QNDiagnostic *diag) {
    if (!program || !qir || !diag) return QN_ERR_RUNTIME;
    memset(qir, 0, sizeof(*qir));

    if (!program->count || program->count > QN_V10_MAX_DECLS) {
        de(diag, "QN-E7816", 1, 1, "invalid V10 AST declaration count");
        return QN_ERR_SEMANTIC;
    }

    uint64_t total = 0u;
    for (uint16_t i = 0u; i < program->count; ++i) {
        QNValueKind kind = program->declarations[i].kind;
        if (kind == QN_VALUE_STRING || kind == QN_VALUE_BYTES) {
            total += program->declarations[i].as.blob.byte_length;
            if (total > QN_V10_MAX_CONSTANT_POOL_BYTES) {
                de(diag, "QN-E7816", program->declarations[i].line,
                   program->declarations[i].column,
                   "V10 constant pool exceeds bounded limit");
                return QN_ERR_LIMIT;
            }
        }
    }

    if (total) {
        qir->constant_bytes = (uint8_t *)malloc((size_t)total);
        if (!qir->constant_bytes) {
            de(diag, "QN-E7816", 1, 1, "out of memory building V10 QIR constant pool");
            return QN_ERR_RUNTIME;
        }
    }

    qir->abi_version = QN_V10_DATA_ABI_V1;
    qir->value_count = program->count;
    uint32_t offset = 0u;

    for (uint16_t i = 0u; i < program->count; ++i) {
        const QNV10DataDecl *source = &program->declarations[i];
        QNV10DataQIRValue *value = &qir->values[i];
        snprintf(value->name, sizeof(value->name), "%s", source->name);
        value->kind = source->kind;

        if (source->kind == QN_VALUE_F32) {
            value->constant_offset = UINT32_MAX;
            value->byte_length = 4u;
            if (source->as.f32 == 0.0f) {
                value->f32_bits = 0u;
            } else {
                memcpy(&value->f32_bits, &source->as.f32, sizeof(value->f32_bits));
            }
        } else if (source->kind == QN_VALUE_STRING || source->kind == QN_VALUE_BYTES) {
            value->constant_offset = offset;
            value->byte_length = source->as.blob.byte_length;
            if (value->byte_length) {
                memcpy(qir->constant_bytes + offset,
                       source->as.blob.data,
                       value->byte_length);
                offset += value->byte_length;
            }
        } else {
            qn_v10_data_qir_free(qir);
            de(diag, "QN-E7816", source->line, source->column,
               "unsupported V10 QIR value kind");
            return QN_ERR_SEMANTIC;
        }
    }

    qir->constant_bytes_size = offset;
    qir->requires_qbc_v10 = true;
    return QN_OK;
}

void qn_v10_data_qir_free(QNV10DataQIRProgram *qir) {
    if (!qir) return;
    free(qir->constant_bytes);
    memset(qir, 0, sizeof(*qir));
}

QNStatus qn_v10_data_qbc_guard(const QNV10DataQIRProgram *qir,
                               QNDiagnostic *diag) {
    if (!qir || !diag) return QN_ERR_RUNTIME;
    if (qir->abi_version != QN_V10_DATA_ABI_V1 ||
        !qir->value_count || !qir->requires_qbc_v10) {
        de(diag, "QN-E7817", 0, 0, "invalid V10 native-data QIR program");
        return QN_ERR_SEMANTIC;
    }
    de(diag, "QN-E7818", 0, 0,
       "V10 native-data QIR requires QBC v10; legacy QBC emission is forbidden");
    return QN_ERR_QBC;
}
