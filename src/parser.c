#include "qn_parser.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const QNTokenList *tokens;
    size_t at;
    QNDiagnostic *diag;
} Parser;

static const QNToken *peek(Parser *p) { return &p->tokens->items[p->at]; }
static const QNToken *prev(Parser *p) { return &p->tokens->items[p->at-1]; }
static bool is(Parser *p, QNTokenKind k) { return peek(p)->kind == k; }
static bool match(Parser *p, QNTokenKind k) { if (is(p,k)) { ++p->at; return true; } return false; }

static const QNToken *expect(Parser *p, QNTokenKind k, const char *what) {
    if (is(p,k)) return &p->tokens->items[p->at++];
    const QNToken *t = peek(p);
    qn_diag_set(p->diag, t->line, t->column, "expected %s, found %s", what, qn_token_kind_name(t->kind));
    return NULL;
}

static bool add_stmt(QNProgram *program, QNStmt stmt, QNDiagnostic *diag) {
    if (program->count >= QN_MAX_STATEMENTS) {
        qn_diag_set(diag, stmt.line, stmt.column, "statement limit exceeded");
        return false;
    }
    if (program->count == program->capacity) {
        size_t cap = program->capacity ? program->capacity * 2 : 128;
        QNStmt *next = realloc(program->items, cap * sizeof(*next));
        if (!next) {
            qn_diag_set(diag, stmt.line, stmt.column, "out of memory");
            return false;
        }
        program->items = next;
        program->capacity = cap;
    }
    program->items[program->count++] = stmt;
    return true;
}

void qn_program_free(QNProgram *program) {
    if (!program) return;
    free(program->items);
    memset(program, 0, sizeof(*program));
}

static bool consume_line_end(Parser *p) {
    if (match(p, TOK_NEWLINE) || is(p, TOK_EOF)) {
        while (match(p, TOK_NEWLINE)) {}
        return true;
    }
    const QNToken *t = peek(p);
    qn_diag_set(p->diag, t->line, t->column, "expected end of line");
    return false;
}

static bool parse_target(Parser *p, QNTarget *out) {
    const QNToken *name = expect(p, TOK_IDENT, "register name");
    if (!name) return false;
    memset(out, 0, sizeof(*out));
    snprintf(out->reg, sizeof(out->reg), "%s", name->text);
    out->line = name->line;
    out->column = name->column;
    if (match(p, TOK_LBRACKET)) {
        const QNToken *idx = expect(p, TOK_INT, "qubit index");
        if (!idx || !expect(p, TOK_RBRACKET, "']'")) return false;
        if (idx->int_value > UINT32_MAX) {
            qn_diag_set(p->diag, idx->line, idx->column, "qubit index too large");
            return false;
        }
        out->has_index = true;
        out->index = (uint32_t)idx->int_value;
    }
    return true;
}

static bool parse_state(const char *text, uint32_t width, uint64_t *basis, QNDiagnostic *diag, int line, int col) {
    size_t len = strlen(text);
    if (len < 3 || text[0] != '|' || text[len-1] != '>') return false;
    size_t bits = len - 2;
    if (bits != width) {
        qn_diag_set(diag, line, col, "state width %zu does not match register width %u", bits, width);
        return false;
    }
    uint64_t v = 0;
    for (size_t i = 1; i + 1 < len; ++i) {
        v = (v << 1) | (uint64_t)(text[i] - '0');
    }
    *basis = v;
    return true;
}

QNStatus qn_parse(const QNTokenList *tokens, QNProgram *out, QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));
    Parser p = {.tokens=tokens,.at=0,.diag=diag};
    while (match(&p, TOK_NEWLINE)) {}

    while (!is(&p, TOK_EOF)) {
        const QNToken *start = peek(&p);
        QNStmt s;
        memset(&s, 0, sizeof(s));
        s.line = start->line;
        s.column = start->column;

        if (match(&p, TOK_QBIT) || match(&p, TOK_QREG)) {
            bool single = prev(&p)->kind == TOK_QBIT;
            s.kind = STMT_QREG;
            const QNToken *name = expect(&p, TOK_IDENT, "register name");
            if (!name) goto fail;
            snprintf(s.as.qreg.name, sizeof(s.as.qreg.name), "%s", name->text);
            s.as.qreg.width = single ? 1u : 0u;

            if (!single) {
                if (!expect(&p, TOK_LBRACKET, "'['")) goto fail;
                const QNToken *width = expect(&p, TOK_INT, "register width");
                if (!width || !expect(&p, TOK_RBRACKET, "']'")) goto fail;
                if (width->int_value == 0 || width->int_value > QN_MAX_QUBITS) {
                    qn_diag_set(diag, width->line, width->column, "register width must be 1..%u", QN_MAX_QUBITS);
                    goto fail;
                }
                s.as.qreg.width = (uint32_t)width->int_value;
            }

            if (match(&p, TOK_ASSIGN)) {
                const QNToken *state = expect(&p, TOK_STATE, "basis state");
                if (!state) goto fail;
                snprintf(s.as.qreg.state_text, sizeof(s.as.qreg.state_text), "%s", state->text);
                if (!parse_state(state->text, s.as.qreg.width, &s.as.qreg.initial_basis,
                                 diag, state->line, state->column)) goto fail;
            } else {
                snprintf(s.as.qreg.state_text, sizeof(s.as.qreg.state_text), "|0>");
                s.as.qreg.initial_basis = 0;
            }
        } else if (match(&p, TOK_H) || match(&p, TOK_X) || match(&p, TOK_Z)) {
            QNTokenKind k = prev(&p)->kind;
            s.kind = k == TOK_H ? STMT_H : (k == TOK_X ? STMT_X : STMT_Z);
            if (!parse_target(&p, &s.as.unary.target)) goto fail;
        } else if (match(&p, TOK_CX)) {
            s.kind = STMT_CX;
            if (!parse_target(&p, &s.as.cx.control) || !parse_target(&p, &s.as.cx.target)) goto fail;
        } else if (match(&p, TOK_GHZ)) {
            s.kind = STMT_GHZ;
            const QNToken *name = expect(&p, TOK_IDENT, "register name");
            if (!name) goto fail;
            snprintf(s.as.ghz.reg, sizeof(s.as.ghz.reg), "%s", name->text);
        } else if (match(&p, TOK_MEASURE)) {
            s.kind = STMT_MEASURE;
            const QNToken *reg = expect(&p, TOK_IDENT, "register name");
            if (!reg || !expect(&p, TOK_ARROW, "'->'")) goto fail;
            const QNToken *output = expect(&p, TOK_IDENT, "result name");
            if (!output) goto fail;
            snprintf(s.as.measure.reg, sizeof(s.as.measure.reg), "%s", reg->text);
            snprintf(s.as.measure.output, sizeof(s.as.measure.output), "%s", output->text);
        } else if (match(&p, TOK_EMIT)) {
            s.kind = STMT_EMIT;
            const QNToken *name = expect(&p, TOK_IDENT, "result name");
            if (!name) goto fail;
            snprintf(s.as.emit.name, sizeof(s.as.emit.name), "%s", name->text);
        } else if (match(&p, TOK_REQUIRES)) {
            s.kind = STMT_REQUIRES;
            const QNToken *first =
                expect(&p, TOK_IDENT, "capability name");
            if (!first) goto fail;

            if (match(&p, TOK_DOT)) {
                const QNToken *second =
                    expect(&p, TOK_IDENT, "capability suffix");
                if (!second) goto fail;

                int written = snprintf(
                    s.as.requires.capability,
                    sizeof(s.as.requires.capability),
                    "%s.%s",
                    first->text,
                    second->text
                );
                if (written < 0 ||
                    (size_t)written >=
                    sizeof(s.as.requires.capability)) {
                    qn_diag_set(
                        diag,
                        first->line,
                        first->column,
                        "capability name is too long"
                    );
                    goto fail;
                }
            } else {
                snprintf(
                    s.as.requires.capability,
                    sizeof(s.as.requires.capability),
                    "%s",
                    first->text
                );
            }
        } else if (match(&p, TOK_SEED) || match(&p, TOK_SHOTS)) {
            QNTokenKind k = prev(&p)->kind;
            s.kind = k == TOK_SEED ? STMT_SEED : STMT_SHOTS;
            const QNToken *n = expect(&p, TOK_INT, "integer");
            if (!n) goto fail;
            s.as.number.value = n->int_value;
        } else if (match(&p, TOK_LET)) {
            s.kind = STMT_U32_LET;
            const QNToken *name = expect(&p, TOK_IDENT, "variable name");
            if (!name || !expect(&p, TOK_COLON, "':'") ||
                !expect(&p, TOK_U32, "u32") ||
                !expect(&p, TOK_EQUAL, "'='")) goto fail;
            const QNToken *value = expect(&p, TOK_INT, "u32 literal");
            if (!value) goto fail;
            if (value->int_value > UINT32_MAX) {
                qn_diag_set_code(diag, "QN-E7500", value->line, value->column,
                                 "u32 literal exceeds 4294967295");
                goto fail;
            }
            snprintf(s.as.u32_let.name, sizeof(s.as.u32_let.name),
                     "%s", name->text);
            s.as.u32_let.value = (uint32_t)value->int_value;
        } else if (is(&p, TOK_IDENT)) {
            const QNToken *output = expect(&p, TOK_IDENT, "output variable");
            s.kind = STMT_U32_ADD;
            if (!expect(&p, TOK_EQUAL, "'='") ) goto fail;
            const QNToken *left = expect(&p, TOK_IDENT, "left variable");
            if (!left || !expect(&p, TOK_PLUS, "'+'")) goto fail;
            const QNToken *right = expect(&p, TOK_IDENT, "right variable");
            if (!right) goto fail;
            snprintf(s.as.u32_add.output, sizeof(s.as.u32_add.output),
                     "%s", output->text);
            snprintf(s.as.u32_add.left, sizeof(s.as.u32_add.left),
                     "%s", left->text);
            snprintf(s.as.u32_add.right, sizeof(s.as.u32_add.right),
                     "%s", right->text);
        } else if (match(&p, TOK_VECTOR_ADD_U32)) {
            s.kind = STMT_VECTOR_ADD_U32;
            if (!expect(&p, TOK_ARROW, "'->'")) goto fail;
            const QNToken *output =
                expect(&p, TOK_IDENT, "vector result name");
            if (!output) goto fail;
            snprintf(
                s.as.vector_add_u32.output,
                sizeof(s.as.vector_add_u32.output),
                "%s",
                output->text
            );
        } else {
            qn_diag_set(diag, start->line, start->column, "unexpected token %s", qn_token_kind_name(start->kind));
            goto fail;
        }

        if (!consume_line_end(&p) || !add_stmt(out, s, diag)) goto fail;
    }
    return QN_OK;

fail:
    qn_program_free(out);
    return QN_ERR_PARSE;
}
