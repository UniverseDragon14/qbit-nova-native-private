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

static void qn_stmt_free_contents(QNStmt *stmt) {
    if (!stmt || stmt->kind != STMT_IF) return;
    for (size_t i = 0; i < stmt->as.if_stmt.then_count; ++i) {
        qn_stmt_free_contents(&stmt->as.if_stmt.then_items[i]);
    }
    for (size_t i = 0; i < stmt->as.if_stmt.else_count; ++i) {
        qn_stmt_free_contents(&stmt->as.if_stmt.else_items[i]);
    }
    free(stmt->as.if_stmt.then_items);
    free(stmt->as.if_stmt.else_items);
    stmt->as.if_stmt.then_items = NULL;
    stmt->as.if_stmt.else_items = NULL;
    stmt->as.if_stmt.then_count = 0u;
    stmt->as.if_stmt.else_count = 0u;
}

void qn_program_free(QNProgram *program) {
    if (!program) return;
    for (size_t i = 0; i < program->count; ++i) {
        qn_stmt_free_contents(&program->items[i]);
    }
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

static bool consume_branch_line_end(Parser *p) {
    if (is(p, TOK_RBRACE)) return true;
    return consume_line_end(p);
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

static bool parse_u32_let_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_U32_LET;
    const QNToken *name = expect(p, TOK_IDENT, "variable name");
    if (!name || !expect(p, TOK_COLON, "':'") ||
        !expect(p, TOK_U32, "u32") ||
        !expect(p, TOK_EQUAL, "'='")) return false;
    const QNToken *value = expect(p, TOK_INT, "u32 literal");
    if (!value) return false;
    if (value->int_value > UINT32_MAX) {
        qn_diag_set_code(p->diag, "QN-E7500", value->line, value->column,
                         "u32 literal exceeds 4294967295");
        return false;
    }
    snprintf(s->as.u32_let.name, sizeof(s->as.u32_let.name),
             "%s", name->text);
    s->as.u32_let.value = (uint32_t)value->int_value;
    return true;
}

static bool parse_scalar_binary(Parser *p, QNStmt *s) {
    const QNToken *output = expect(p, TOK_IDENT, "output variable");
    if (!output || !expect(p, TOK_EQUAL, "'='")) return false;
    const QNToken *left = expect(p, TOK_IDENT, "left variable");
    if (!left) return false;
    if (match(p, TOK_PLUS)) {
        s->kind = STMT_U32_ADD;
    } else if (match(p, TOK_MINUS)) {
        s->kind = STMT_U32_SUB;
    } else if (match(p, TOK_STAR)) {
        s->kind = STMT_U32_MUL;
    } else if (match(p, TOK_SLASH)) {
        s->kind = STMT_U32_DIV;
    } else if (match(p, TOK_EQ_EQ)) {
        s->kind = STMT_U32_EQ;
    } else if (match(p, TOK_BANG_EQUAL)) {
        s->kind = STMT_U32_NE;
    } else if (match(p, TOK_LT)) {
        s->kind = STMT_U32_LT;
    } else if (match(p, TOK_LT_EQUAL)) {
        s->kind = STMT_U32_LE;
    } else if (match(p, TOK_GT)) {
        s->kind = STMT_U32_GT;
    } else if (match(p, TOK_GT_EQUAL)) {
        s->kind = STMT_U32_GE;
    } else {
        const QNToken *t = peek(p);
        qn_diag_set(p->diag, t->line, t->column,
                    "expected arithmetic or comparison operator, found %s",
                    qn_token_kind_name(t->kind));
        return false;
    }
    const QNToken *right = expect(p, TOK_IDENT, "right variable");
    if (!right) return false;
    snprintf(s->as.scalar_binary.output, sizeof(s->as.scalar_binary.output),
             "%s", output->text);
    snprintf(s->as.scalar_binary.left, sizeof(s->as.scalar_binary.left),
             "%s", left->text);
    snprintf(s->as.scalar_binary.right, sizeof(s->as.scalar_binary.right),
             "%s", right->text);
    return true;
}

static bool parse_branch_statement(Parser *p, QNStmt *s) {
    const QNToken *start = peek(p);
    memset(s, 0, sizeof(*s));
    s->line = start->line;
    s->column = start->column;

    if (match(p, TOK_IF)) {
        qn_diag_set_code(p->diag, "QN-E7536", start->line, start->column,
                         "nested if is not enabled in Stage 7 Step 4");
        return false;
    }
    if (match(p, TOK_LET)) {
        return parse_u32_let_after_keyword(p, s);
    }
    if (is(p, TOK_IDENT)) {
        return parse_scalar_binary(p, s);
    }
    if (match(p, TOK_EMIT)) {
        s->kind = STMT_EMIT;
        const QNToken *name = expect(p, TOK_IDENT, "result name");
        if (!name) return false;
        snprintf(s->as.emit.name, sizeof(s->as.emit.name), "%s", name->text);
        return true;
    }

    qn_diag_set_code(p->diag, "QN-E7537", start->line, start->column,
                     "Step4 if/else branches permit only let, scalar expressions, and emit");
    return false;
}

static bool parse_scalar_block(Parser *p,
                               QNStmt **items_out,
                               size_t *count_out) {
    QNProgram block = {0};
    while (match(p, TOK_NEWLINE)) {}

    while (!is(p, TOK_RBRACE)) {
        if (is(p, TOK_EOF)) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7533", t->line, t->column,
                             "unterminated if/else block; expected '}'");
            qn_program_free(&block);
            return false;
        }

        QNStmt stmt;
        if (!parse_branch_statement(p, &stmt) ||
            !consume_branch_line_end(p) ||
            !add_stmt(&block, stmt, p->diag)) {
            qn_stmt_free_contents(&stmt);
            qn_program_free(&block);
            return false;
        }
        while (match(p, TOK_NEWLINE)) {}
    }

    (void)match(p, TOK_RBRACE);
    *items_out = block.items;
    *count_out = block.count;
    return true;
}

static bool parse_if_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_IF;
    const QNToken *condition = expect(p, TOK_IDENT, "bool condition variable");
    if (!condition) return false;
    snprintf(s->as.if_stmt.condition, sizeof(s->as.if_stmt.condition),
             "%s", condition->text);

    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7533", t->line, t->column,
                         "expected '{' to start if block");
        return false;
    }
    if (!parse_scalar_block(p,
                            &s->as.if_stmt.then_items,
                            &s->as.if_stmt.then_count)) {
        qn_stmt_free_contents(s);
        return false;
    }

    while (match(p, TOK_NEWLINE)) {}
    if (!match(p, TOK_ELSE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7532", t->line, t->column,
                         "Stage 7 Step 4 requires an else block");
        qn_stmt_free_contents(s);
        return false;
    }
    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7533", t->line, t->column,
                         "expected '{' to start else block");
        qn_stmt_free_contents(s);
        return false;
    }
    if (!parse_scalar_block(p,
                            &s->as.if_stmt.else_items,
                            &s->as.if_stmt.else_count)) {
        qn_stmt_free_contents(s);
        return false;
    }
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
            const QNToken *first = expect(&p, TOK_IDENT, "capability name");
            if (!first) goto fail;

            if (match(&p, TOK_DOT)) {
                const QNToken *second = expect(&p, TOK_IDENT, "capability suffix");
                if (!second) goto fail;
                int written = snprintf(s.as.requires.capability,
                                       sizeof(s.as.requires.capability),
                                       "%s.%s", first->text, second->text);
                if (written < 0 ||
                    (size_t)written >= sizeof(s.as.requires.capability)) {
                    qn_diag_set(diag, first->line, first->column,
                                "capability name is too long");
                    goto fail;
                }
            } else {
                snprintf(s.as.requires.capability,
                         sizeof(s.as.requires.capability), "%s", first->text);
            }
        } else if (match(&p, TOK_SEED) || match(&p, TOK_SHOTS)) {
            QNTokenKind k = prev(&p)->kind;
            s.kind = k == TOK_SEED ? STMT_SEED : STMT_SHOTS;
            const QNToken *n = expect(&p, TOK_INT, "integer");
            if (!n) goto fail;
            s.as.number.value = n->int_value;
        } else if (match(&p, TOK_LET)) {
            if (!parse_u32_let_after_keyword(&p, &s)) goto fail;
        } else if (match(&p, TOK_IF)) {
            if (!parse_if_after_keyword(&p, &s)) goto fail;
        } else if (is(&p, TOK_IDENT)) {
            if (!parse_scalar_binary(&p, &s)) goto fail;
        } else if (match(&p, TOK_VECTOR_ADD_U32)) {
            s.kind = STMT_VECTOR_ADD_U32;
            if (!expect(&p, TOK_ARROW, "'->'")) goto fail;
            const QNToken *output = expect(&p, TOK_IDENT, "vector result name");
            if (!output) goto fail;
            snprintf(s.as.vector_add_u32.output,
                     sizeof(s.as.vector_add_u32.output),
                     "%s", output->text);
        } else {
            qn_diag_set(diag, start->line, start->column,
                        "unexpected token %s", qn_token_kind_name(start->kind));
            goto fail;
        }

        if (!consume_line_end(&p) || !add_stmt(out, s, diag)) {
            qn_stmt_free_contents(&s);
            goto fail;
        }
        if (s.kind == STMT_IF && !is(&p, TOK_EOF)) {
            const QNToken *t = peek(&p);
            qn_diag_set_code(diag, "QN-E7535", t->line, t->column,
                             "Stage 7 Step 4 if/else must terminate the scalar program");
            goto fail;
        }
    }
    return QN_OK;

fail:
    qn_program_free(out);
    return QN_ERR_PARSE;
}
