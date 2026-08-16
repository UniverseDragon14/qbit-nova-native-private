#include "qn_parser.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const QNTokenList *tokens;
    size_t at;
    QNDiagnostic *diag;
} Parser;

static const QNToken *peek(Parser *p) { return &p->tokens->items[p->at]; }
static const QNToken *prev(Parser *p) { return &p->tokens->items[p->at - 1u]; }
static bool is(Parser *p, QNTokenKind k) { return peek(p)->kind == k; }
static bool match(Parser *p, QNTokenKind k) {
    if (is(p, k)) {
        ++p->at;
        return true;
    }
    return false;
}

static bool is_contextual_input_declaration(Parser *p) {
    const QNToken *current = peek(p);

    if (current->kind != TOK_IDENT ||
        strcmp(current->text, "input") != 0) {
        return false;
    }

    if (p->at + 1u >= p->tokens->count) {
        return false;
    }

    /*
     * "input" remains a legal frozen identifier.
     *
     * Only statement-position syntax shaped like:
     *
     *     input <identifier> ...
     *
     * is interpreted as a Step7 runtime-input declaration.
     *
     * Examples preserved as identifiers:
     *
     *     let input: u32 = 9
     *     input = left + right
     *     call input(value) -> result
     */
    return p->tokens->items[p->at + 1u].kind == TOK_IDENT;
}

static bool match_contextual_input_declaration(Parser *p) {
    if (!is_contextual_input_declaration(p)) {
        return false;
    }

    ++p->at;
    return true;
}

static const QNToken *expect(Parser *p, QNTokenKind k, const char *what) {
    if (is(p, k)) return &p->tokens->items[p->at++];
    const QNToken *t = peek(p);
    qn_diag_set(p->diag, t->line, t->column,
                "expected %s, found %s", what,
                qn_token_kind_name(t->kind));
    return NULL;
}

static bool add_stmt(QNProgram *program, QNStmt stmt, QNDiagnostic *diag) {
    if (program->count >= QN_MAX_STATEMENTS) {
        qn_diag_set(diag, stmt.line, stmt.column, "statement limit exceeded");
        return false;
    }
    if (program->count == program->capacity) {
        size_t cap = program->capacity ? program->capacity * 2u : 128u;
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
    if (!stmt) return;
    if (stmt->kind == STMT_IF) {
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
        return;
    }
    if (stmt->kind == STMT_REPEAT) {
        for (size_t i = 0; i < stmt->as.repeat_stmt.body_count; ++i) {
            qn_stmt_free_contents(&stmt->as.repeat_stmt.body_items[i]);
        }
        free(stmt->as.repeat_stmt.body_items);
        stmt->as.repeat_stmt.body_items = NULL;
        stmt->as.repeat_stmt.body_count = 0u;
    }
}

void qn_program_free(QNProgram *program) {
    if (!program) return;
    for (size_t i = 0; i < program->count; ++i) {
        qn_stmt_free_contents(&program->items[i]);
    }
    for (size_t i = 0; i < program->function_count; ++i) {
        QNFunctionDecl *fn = &program->functions[i];
        for (size_t j = 0; j < fn->body_count; ++j) {
            qn_stmt_free_contents(&fn->body_items[j]);
        }
        free(fn->body_items);
        fn->body_items = NULL;
        fn->body_count = 0u;
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
            qn_diag_set(p->diag, idx->line, idx->column,
                        "qubit index too large");
            return false;
        }
        out->has_index = true;
        out->index = (uint32_t)idx->int_value;
    }
    return true;
}

static bool parse_state(const char *text, uint32_t width,
                        uint64_t *basis, QNDiagnostic *diag,
                        int line, int col) {
    size_t len = strlen(text);
    if (len < 3u || text[0] != '|' || text[len - 1u] != '>') return false;
    size_t bits = len - 2u;
    if (bits != width) {
        qn_diag_set(diag, line, col,
                    "state width %zu does not match register width %u",
                    bits, width);
        return false;
    }
    uint64_t v = 0u;
    for (size_t i = 1u; i + 1u < len; ++i) {
        v = (v << 1u) | (uint64_t)(text[i] - '0');
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

/*
 * Step9 tensor syntax stays contextual so frozen identifiers remain legal.
 *
 * Recognized declaration:
 *
 *     tensor weights: f32[4]
 *     tensor bytes: i8[32]
 *
 * The word "tensor" is NOT promoted to a lexer keyword.
 */
static bool step9_is_word(const QNToken *token, const char *word) {
    return token &&
           token->kind == TOK_IDENT &&
           strcmp(token->text, word) == 0;
}

static bool step9_is_tensor_declaration(Parser *p) {
    if (!p || p->at + 3u >= p->tokens->count) return false;

    const QNToken *keyword = &p->tokens->items[p->at];
    const QNToken *name = &p->tokens->items[p->at + 1u];
    const QNToken *colon = &p->tokens->items[p->at + 2u];
    const QNToken *type = &p->tokens->items[p->at + 3u];

    if (!step9_is_word(keyword, "tensor") ||
        name->kind != TOK_IDENT ||
        colon->kind != TOK_COLON ||
        type->kind != TOK_IDENT) {
        return false;
    }

    return strcmp(type->text, "f32") == 0 ||
           strcmp(type->text, "i8") == 0;
}

static bool parse_step9_tensor_declaration(Parser *p, QNStmt *s) {
    if (!p || !s || !step9_is_tensor_declaration(p)) return false;

    const QNToken *keyword = peek(p);
    (void)keyword;
    p->at++;

    const QNToken *name = expect(p, TOK_IDENT, "tensor name");
    if (!name ||
        !expect(p, TOK_COLON, "':'")) {
        return false;
    }

    const QNToken *type = expect(p, TOK_IDENT, "tensor element type");
    if (!type) return false;

    QNTensorElementType element_type;
    if (strcmp(type->text, "f32") == 0) {
        element_type = QN_TENSOR_ELEMENT_F32;
    } else if (strcmp(type->text, "i8") == 0) {
        element_type = QN_TENSOR_ELEMENT_I8;
    } else {
        qn_diag_set_code(
            p->diag,
            "QN-E7701",
            type->line,
            type->column,
            "Step9 tensor element type must be f32 or i8"
        );
        return false;
    }

    if (!expect(p, TOK_LBRACKET, "'['")) return false;

    const QNToken *count = expect(p, TOK_INT, "tensor element count");
    if (!count) return false;

    if (!expect(p, TOK_RBRACKET, "']'")) return false;

    if (count->int_value == 0u ||
        count->int_value > QN_MAX_TENSOR_ELEMENTS) {
        qn_diag_set_code(
            p->diag,
            "QN-E7702",
            count->line,
            count->column,
            "Step9 tensor element count must be 1..%u",
            QN_MAX_TENSOR_ELEMENTS
        );
        return false;
    }

    s->kind = STMT_TENSOR_DECL;
    snprintf(
        s->as.tensor_decl.name,
        sizeof(s->as.tensor_decl.name),
        "%s",
        name->text
    );
    s->as.tensor_decl.element_type = element_type;
    s->as.tensor_decl.element_count = (uint32_t)count->int_value;

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

static bool parse_call_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_CALL;
    const QNToken *name = expect(p, TOK_IDENT, "function name");
    if (!name || !expect(p, TOK_LPAREN, "'('") ) return false;
    snprintf(s->as.call.function, sizeof(s->as.call.function),
             "%s", name->text);

    uint8_t count = 0u;
    if (!is(p, TOK_RPAREN)) {
        for (;;) {
            if (count >= QN_MAX_FUNCTION_PARAMS) {
                const QNToken *t = peek(p);
                qn_diag_set_code(p->diag, "QN-E7572", t->line, t->column,
                                 "function calls support at most %u arguments",
                                 QN_MAX_FUNCTION_PARAMS);
                return false;
            }
            const QNToken *arg = expect(p, TOK_IDENT, "u32 argument");
            if (!arg) return false;
            snprintf(s->as.call.args[count], sizeof(s->as.call.args[count]),
                     "%s", arg->text);
            ++count;
            if (!match(p, TOK_COMMA)) break;
        }
    }
    if (!expect(p, TOK_RPAREN, "')'") ||
        !expect(p, TOK_ARROW, "'->'")) return false;
    const QNToken *output = expect(p, TOK_IDENT, "call result variable");
    if (!output) return false;
    s->as.call.arg_count = count;
    snprintf(s->as.call.output, sizeof(s->as.call.output),
             "%s", output->text);
    return true;
}

static bool parse_return_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_RETURN;
    const QNToken *name = expect(p, TOK_IDENT, "u32 return value");
    if (!name) return false;
    snprintf(s->as.return_stmt.name, sizeof(s->as.return_stmt.name),
             "%s", name->text);
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
    if (match(p, TOK_REPEAT)) {
        qn_diag_set_code(p->diag, "QN-E7555", start->line, start->column,
                         "repeat inside if is not enabled in Stage 7 Step 5");
        return false;
    }
    if (match(p, TOK_LET)) return parse_u32_let_after_keyword(p, s);
    if (is(p, TOK_IDENT)) return parse_scalar_binary(p, s);
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

static bool parse_scalar_block(Parser *p, QNStmt **items_out, size_t *count_out) {
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

static bool parse_set_after_keyword(Parser *p, QNStmt *s) {
    const QNToken *target = expect(p, TOK_IDENT, "set target variable");
    if (!target || !expect(p, TOK_EQUAL, "'='")) return false;
    const QNToken *left = expect(p, TOK_IDENT, "left variable");
    if (!left) return false;
    if (match(p, TOK_PLUS)) s->kind = STMT_U32_SET_ADD;
    else if (match(p, TOK_MINUS)) s->kind = STMT_U32_SET_SUB;
    else if (match(p, TOK_STAR)) s->kind = STMT_U32_SET_MUL;
    else if (match(p, TOK_SLASH)) s->kind = STMT_U32_SET_DIV;
    else {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7559", t->line, t->column,
                         "set requires one of +, -, *, /");
        return false;
    }
    const QNToken *right = expect(p, TOK_IDENT, "right variable");
    if (!right) return false;
    snprintf(s->as.scalar_binary.output, sizeof(s->as.scalar_binary.output),
             "%s", target->text);
    snprintf(s->as.scalar_binary.left, sizeof(s->as.scalar_binary.left),
             "%s", left->text);
    snprintf(s->as.scalar_binary.right, sizeof(s->as.scalar_binary.right),
             "%s", right->text);
    return true;
}

static bool parse_repeat_body_statement(Parser *p, QNStmt *s) {
    const QNToken *start = peek(p);
    memset(s, 0, sizeof(*s));
    s->line = start->line;
    s->column = start->column;
    if (match(p, TOK_REPEAT)) {
        qn_diag_set_code(p->diag, "QN-E7554", start->line, start->column,
                         "nested repeat is not enabled in Stage 7 Step 5");
        return false;
    }
    if (match(p, TOK_IF)) {
        qn_diag_set_code(p->diag, "QN-E7555", start->line, start->column,
                         "if inside repeat is not enabled in Stage 7 Step 5");
        return false;
    }
    if (match(p, TOK_LET)) {
        qn_diag_set_code(p->diag, "QN-E7556", start->line, start->column,
                         "let inside repeat is not enabled in Stage 7 Step 5");
        return false;
    }
    if (match(p, TOK_EMIT)) {
        qn_diag_set_code(p->diag, "QN-E7557", start->line, start->column,
                         "emit inside repeat is not enabled in Stage 7 Step 5");
        return false;
    }
    if (match(p, TOK_SET)) return parse_set_after_keyword(p, s);
    qn_diag_set_code(p->diag, "QN-E7559", start->line, start->column,
                     "repeat body permits only explicit set statements");
    return false;
}

static bool parse_repeat_block(Parser *p, QNStmt **items_out, size_t *count_out) {
    QNProgram block = {0};
    while (match(p, TOK_NEWLINE)) {}
    while (!is(p, TOK_RBRACE)) {
        if (is(p, TOK_EOF)) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7552", t->line, t->column,
                             "unterminated repeat block; expected '}'");
            qn_program_free(&block);
            return false;
        }
        QNStmt stmt;
        if (!parse_repeat_body_statement(p, &stmt) ||
            !consume_branch_line_end(p) ||
            !add_stmt(&block, stmt, p->diag)) {
            qn_stmt_free_contents(&stmt);
            qn_program_free(&block);
            return false;
        }
        while (match(p, TOK_NEWLINE)) {}
    }
    (void)match(p, TOK_RBRACE);
    if (block.count == 0u) {
        const QNToken *t = prev(p);
        qn_diag_set_code(p->diag, "QN-E7553", t->line, t->column,
                         "repeat body must contain at least one set statement");
        qn_program_free(&block);
        return false;
    }
    *items_out = block.items;
    *count_out = block.count;
    return true;
}

static bool parse_repeat_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_REPEAT;
    const QNToken *count = expect(p, TOK_INT, "repeat iteration literal");
    if (!count) return false;
    if (count->int_value == 0u || count->int_value > QN_MAX_REPEAT_ITERATIONS) {
        qn_diag_set_code(p->diag, "QN-E7550", count->line, count->column,
                         "repeat count must be 1..%u", QN_MAX_REPEAT_ITERATIONS);
        return false;
    }
    s->as.repeat_stmt.iterations = (uint32_t)count->int_value;
    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7552", t->line, t->column,
                         "expected '{' to start repeat block");
        return false;
    }
    if (!parse_repeat_block(p, &s->as.repeat_stmt.body_items,
                            &s->as.repeat_stmt.body_count)) {
        qn_stmt_free_contents(s);
        return false;
    }
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
    if (!parse_scalar_block(p, &s->as.if_stmt.then_items,
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
    if (!parse_scalar_block(p, &s->as.if_stmt.else_items,
                            &s->as.if_stmt.else_count)) {
        qn_stmt_free_contents(s);
        return false;
    }
    return true;
}

static bool function_name_exists(const QNProgram *program, const char *name) {
    for (size_t i = 0; i < program->function_count; ++i) {
        if (strcmp(program->functions[i].name, name) == 0) return true;
    }
    return false;
}

static bool input_name_exists(const QNProgram *program, const char *name) {
    for (size_t i = 0u; i < program->input_count; ++i) {
        if (strcmp(program->inputs[i].name, name) == 0) return true;
    }
    return false;
}

static bool parse_input_after_keyword(Parser *p,
                                      QNProgram *program,
                                      const QNToken *start) {
    if (program->input_count >= QN_MAX_RUNTIME_INPUTS) {
        qn_diag_set_code(p->diag, "QN-E7600", start->line, start->column,
                         "runtime input limit exceeded (%u)",
                         QN_MAX_RUNTIME_INPUTS);
        return false;
    }

    const QNToken *name = expect(p, TOK_IDENT, "runtime input name");
    if (!name || !expect(p, TOK_COLON, "':'")) return false;

    if (!match(p, TOK_U32)) {
        const QNToken *type = peek(p);
        qn_diag_set_code(p->diag, "QN-E7604", type->line, type->column,
                         "Stage 7 Step 7 runtime inputs support u32 only");
        return false;
    }

    if (input_name_exists(program, name->text)) {
        qn_diag_set_code(p->diag, "QN-E7603", name->line, name->column,
                         "duplicate runtime input '%s'", name->text);
        return false;
    }

    QNInputDecl *decl = &program->inputs[program->input_count++];
    memset(decl, 0, sizeof(*decl));
    snprintf(decl->name, sizeof(decl->name), "%s", name->text);
    decl->line = start->line;
    decl->column = start->column;
    qn_sha256((const uint8_t *)name->text,
              strlen(name->text),
              decl->name_sha256);
    return true;
}

static bool parse_step8_function_branch_statement(Parser *p, QNStmt *s) {
    const QNToken *start = peek(p);
    memset(s, 0, sizeof(*s));
    s->line = start->line;
    s->column = start->column;

    if (match(p, TOK_SET)) return parse_set_after_keyword(p, s);
    if (match(p, TOK_RETURN)) return parse_return_after_keyword(p, s);

    if (match(p, TOK_IF)) {
        qn_diag_set_code(p->diag, "QN-E7620", start->line, start->column,
                         "nested if is not enabled inside Step8 function branches");
        return false;
    }
    if (match(p, TOK_REPEAT)) {
        qn_diag_set_code(p->diag, "QN-E7620", start->line, start->column,
                         "repeat is not enabled inside Step8 function branches");
        return false;
    }
    if (match(p, TOK_LET)) {
        qn_diag_set_code(p->diag, "QN-E7620", start->line, start->column,
                         "branch-local let is not enabled in Step8 functions");
        return false;
    }
    if (match(p, TOK_CALL)) {
        qn_diag_set_code(p->diag, "QN-E7620", start->line, start->column,
                         "branch-local call is not enabled in Step8 functions");
        return false;
    }
    if (match(p, TOK_EMIT)) {
        qn_diag_set_code(p->diag, "QN-E7574", start->line, start->column,
                         "emit is not allowed inside functions");
        return false;
    }

    qn_diag_set_code(p->diag, "QN-E7620", start->line, start->column,
                     "Step8 function branches permit only set and return");
    return false;
}

static bool parse_step8_function_branch_block(Parser *p,
                                               QNStmt **items_out,
                                               size_t *count_out) {
    QNProgram block = {0};
    bool seen_return = false;
    while (match(p, TOK_NEWLINE)) {}

    while (!is(p, TOK_RBRACE)) {
        if (is(p, TOK_EOF)) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7533", t->line, t->column,
                             "unterminated Step8 function branch; expected '}'");
            qn_program_free(&block);
            return false;
        }
        if (seen_return) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7621", t->line, t->column,
                             "statement after terminal return in function branch");
            qn_program_free(&block);
            return false;
        }

        QNStmt stmt;
        if (!parse_step8_function_branch_statement(p, &stmt) ||
            !consume_branch_line_end(p) ||
            !add_stmt(&block, stmt, p->diag)) {
            qn_stmt_free_contents(&stmt);
            qn_program_free(&block);
            return false;
        }
        if (stmt.kind == STMT_RETURN) seen_return = true;
        while (match(p, TOK_NEWLINE)) {}
    }

    (void)match(p, TOK_RBRACE);
    *items_out = block.items;
    *count_out = block.count;
    return true;
}

static bool parse_step8_function_if_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_IF;
    const QNToken *condition = expect(p, TOK_IDENT, "bool condition variable");
    if (!condition) return false;
    snprintf(s->as.if_stmt.condition, sizeof(s->as.if_stmt.condition),
             "%s", condition->text);

    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7533", t->line, t->column,
                         "expected '{' to start function if block");
        return false;
    }
    if (!parse_step8_function_branch_block(p,
                                            &s->as.if_stmt.then_items,
                                            &s->as.if_stmt.then_count)) {
        qn_stmt_free_contents(s);
        return false;
    }

    while (match(p, TOK_NEWLINE)) {}
    if (!match(p, TOK_ELSE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7532", t->line, t->column,
                         "Step8 function if requires an else block");
        qn_stmt_free_contents(s);
        return false;
    }
    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7533", t->line, t->column,
                         "expected '{' to start function else block");
        qn_stmt_free_contents(s);
        return false;
    }
    if (!parse_step8_function_branch_block(p,
                                            &s->as.if_stmt.else_items,
                                            &s->as.if_stmt.else_count)) {
        qn_stmt_free_contents(s);
        return false;
    }
    return true;
}

static bool parse_step8_function_repeat_body_statement(Parser *p, QNStmt *s) {
    const QNToken *start = peek(p);
    memset(s, 0, sizeof(*s));
    s->line = start->line;
    s->column = start->column;

    if (match(p, TOK_SET)) return parse_set_after_keyword(p, s);

    if (match(p, TOK_RETURN)) {
        qn_diag_set_code(p->diag, "QN-E7622", start->line, start->column,
                         "return inside function repeat is not enabled in Step8");
        return false;
    }
    if (match(p, TOK_REPEAT)) {
        qn_diag_set_code(p->diag, "QN-E7554", start->line, start->column,
                         "nested repeat is not enabled in Step8 functions");
        return false;
    }
    if (match(p, TOK_IF)) {
        qn_diag_set_code(p->diag, "QN-E7555", start->line, start->column,
                         "if inside function repeat is not enabled in Step8");
        return false;
    }
    if (match(p, TOK_LET) || match(p, TOK_CALL) || match(p, TOK_EMIT)) {
        qn_diag_set_code(p->diag, "QN-E7622", start->line, start->column,
                         "Step8 function repeat body permits only set statements");
        return false;
    }

    qn_diag_set_code(p->diag, "QN-E7622", start->line, start->column,
                     "Step8 function repeat body permits only set statements");
    return false;
}

static bool parse_step8_function_repeat_block(Parser *p,
                                               QNStmt **items_out,
                                               size_t *count_out) {
    QNProgram block = {0};
    while (match(p, TOK_NEWLINE)) {}

    while (!is(p, TOK_RBRACE)) {
        if (is(p, TOK_EOF)) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7552", t->line, t->column,
                             "unterminated function repeat block; expected '}'");
            qn_program_free(&block);
            return false;
        }
        QNStmt stmt;
        if (!parse_step8_function_repeat_body_statement(p, &stmt) ||
            !consume_branch_line_end(p) ||
            !add_stmt(&block, stmt, p->diag)) {
            qn_stmt_free_contents(&stmt);
            qn_program_free(&block);
            return false;
        }
        while (match(p, TOK_NEWLINE)) {}
    }

    (void)match(p, TOK_RBRACE);
    if (block.count == 0u) {
        const QNToken *t = prev(p);
        qn_diag_set_code(p->diag, "QN-E7553", t->line, t->column,
                         "function repeat body must contain at least one set statement");
        qn_program_free(&block);
        return false;
    }
    *items_out = block.items;
    *count_out = block.count;
    return true;
}

static bool parse_step8_function_repeat_after_keyword(Parser *p, QNStmt *s) {
    s->kind = STMT_REPEAT;
    const QNToken *count = expect(p, TOK_INT, "repeat iteration literal");
    if (!count) return false;
    if (count->int_value == 0u || count->int_value > QN_MAX_REPEAT_ITERATIONS) {
        qn_diag_set_code(p->diag, "QN-E7550", count->line, count->column,
                         "repeat count must be 1..%u", QN_MAX_REPEAT_ITERATIONS);
        return false;
    }
    s->as.repeat_stmt.iterations = (uint32_t)count->int_value;
    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7552", t->line, t->column,
                         "expected '{' to start function repeat block");
        return false;
    }
    if (!parse_step8_function_repeat_block(p,
                                            &s->as.repeat_stmt.body_items,
                                            &s->as.repeat_stmt.body_count)) {
        qn_stmt_free_contents(s);
        return false;
    }
    return true;
}

static bool parse_function_body_statement(Parser *p, QNStmt *s) {
    const QNToken *start = peek(p);
    memset(s, 0, sizeof(*s));
    s->line = start->line;
    s->column = start->column;

    if (match(p, TOK_FN)) {
        qn_diag_set_code(p->diag, "QN-E7573", start->line, start->column,
                         "nested function definitions are not enabled");
        return false;
    }
    if (match_contextual_input_declaration(p)) {
        qn_diag_set_code(p->diag, "QN-E7605", start->line, start->column,
                         "runtime input declarations are top-level only");
        return false;
    }
    if (match(p, TOK_EMIT)) {
        qn_diag_set_code(p->diag, "QN-E7574", start->line, start->column,
                         "emit is not allowed inside functions");
        return false;
    }
    if (match(p, TOK_IF)) return parse_step8_function_if_after_keyword(p, s);
    if (match(p, TOK_REPEAT)) return parse_step8_function_repeat_after_keyword(p, s);
    if (match(p, TOK_SET)) return parse_set_after_keyword(p, s);
    if (match(p, TOK_LET)) return parse_u32_let_after_keyword(p, s);
    if (match(p, TOK_CALL)) return parse_call_after_keyword(p, s);
    if (match(p, TOK_RETURN)) return parse_return_after_keyword(p, s);
    if (is(p, TOK_IDENT)) return parse_scalar_binary(p, s);

    qn_diag_set_code(p->diag, "QN-E7575", start->line, start->column,
                     "unsupported statement inside Step8 function");
    return false;
}

static bool parse_function_block(Parser *p,
                                 QNStmt **items_out,
                                 size_t *count_out) {
    QNProgram block = {0};
    bool direct_return_seen = false;
    while (match(p, TOK_NEWLINE)) {}

    while (!is(p, TOK_RBRACE)) {
        if (is(p, TOK_EOF)) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7577", t->line, t->column,
                             "unterminated function body; expected '}'");
            qn_program_free(&block);
            return false;
        }
        if (direct_return_seen) {
            const QNToken *t = peek(p);
            qn_diag_set_code(p->diag, "QN-E7578", t->line, t->column,
                             "statement after terminal function return");
            qn_program_free(&block);
            return false;
        }

        QNStmt stmt;
        if (!parse_function_body_statement(p, &stmt) ||
            !consume_branch_line_end(p) ||
            !add_stmt(&block, stmt, p->diag)) {
            qn_stmt_free_contents(&stmt);
            qn_program_free(&block);
            return false;
        }
        if (stmt.kind == STMT_RETURN) direct_return_seen = true;
        while (match(p, TOK_NEWLINE)) {}
    }

    (void)match(p, TOK_RBRACE);
    *items_out = block.items;
    *count_out = block.count;
    return true;
}

static bool parse_function_after_keyword(Parser *p,
                                         QNProgram *program,
                                         const QNToken *start) {
    if (program->function_count >= QN_MAX_FUNCTIONS) {
        qn_diag_set_code(p->diag, "QN-E7570", start->line, start->column,
                         "function limit exceeded (%u)", QN_MAX_FUNCTIONS);
        return false;
    }
    const QNToken *name = expect(p, TOK_IDENT, "function name");
    if (!name) return false;
    if (function_name_exists(program, name->text)) {
        qn_diag_set_code(p->diag, "QN-E7571", name->line, name->column,
                         "duplicate function name '%s'", name->text);
        return false;
    }
    if (!expect(p, TOK_LPAREN, "'('")) return false;

    QNFunctionDecl fn;
    memset(&fn, 0, sizeof(fn));
    fn.line = start->line;
    fn.column = start->column;
    snprintf(fn.name, sizeof(fn.name), "%s", name->text);

    if (!is(p, TOK_RPAREN)) {
        for (;;) {
            if (fn.param_count >= QN_MAX_FUNCTION_PARAMS) {
                const QNToken *t = peek(p);
                qn_diag_set_code(p->diag, "QN-E7572", t->line, t->column,
                                 "functions support at most %u parameters",
                                 QN_MAX_FUNCTION_PARAMS);
                return false;
            }
            const QNToken *param = expect(p, TOK_IDENT, "parameter name");
            if (!param || !expect(p, TOK_COLON, "':'") ||
                !expect(p, TOK_U32, "u32")) return false;
            for (uint8_t i = 0u; i < fn.param_count; ++i) {
                if (strcmp(fn.params[i], param->text) == 0) {
                    qn_diag_set_code(p->diag, "QN-E7580",
                                     param->line, param->column,
                                     "duplicate parameter name '%s'",
                                     param->text);
                    return false;
                }
            }
            snprintf(fn.params[fn.param_count],
                     sizeof(fn.params[fn.param_count]), "%s", param->text);
            ++fn.param_count;
            if (!match(p, TOK_COMMA)) break;
        }
    }

    if (!expect(p, TOK_RPAREN, "')'") ||
        !expect(p, TOK_ARROW, "'->'") ||
        !expect(p, TOK_U32, "u32 return type")) return false;
    if (!match(p, TOK_LBRACE)) {
        const QNToken *t = peek(p);
        qn_diag_set_code(p->diag, "QN-E7577", t->line, t->column,
                         "expected '{' to start function body");
        return false;
    }
    if (!parse_function_block(p, &fn.body_items, &fn.body_count)) {
        for (size_t i = 0; i < fn.body_count; ++i) {
            qn_stmt_free_contents(&fn.body_items[i]);
        }
        free(fn.body_items);
        return false;
    }
    program->functions[program->function_count++] = fn;
    return true;
}

QNStatus qn_parse(const QNTokenList *tokens, QNProgram *out,
                  QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));
    Parser p = {.tokens = tokens, .at = 0u, .diag = diag};
    bool seen_repeat = false;
    bool seen_inputs = false;
    bool seen_main = false;
    while (match(&p, TOK_NEWLINE)) {}

    while (!is(&p, TOK_EOF)) {
        const QNToken *start = peek(&p);

        if (match(&p, TOK_FN)) {
            if (seen_inputs || seen_main) {
                qn_diag_set_code(diag, "QN-E7581", start->line, start->column,
                                 "function definitions must appear before runtime inputs and main statements");
                goto fail;
            }
            if (!parse_function_after_keyword(&p, out, start) ||
                !consume_line_end(&p)) goto fail;
            continue;
        }

        if (match_contextual_input_declaration(&p)) {
            if (seen_main) {
                qn_diag_set_code(diag, "QN-E7602", start->line, start->column,
                                 "runtime input declarations must appear before main statements");
                goto fail;
            }
            seen_inputs = true;
            if (!parse_input_after_keyword(&p, out, start) ||
                !consume_line_end(&p)) goto fail;
            continue;
        }

        seen_main = true;
        QNStmt s;
        memset(&s, 0, sizeof(s));
        s.line = start->line;
        s.column = start->column;

        if (step9_is_tensor_declaration(&p)) {
            if (!parse_step9_tensor_declaration(&p, &s)) goto fail;
        } else if (match(&p, TOK_QBIT) || match(&p, TOK_QREG)) {
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
                if (width->int_value == 0u || width->int_value > QN_MAX_QUBITS) {
                    qn_diag_set(diag, width->line, width->column,
                                "register width must be 1..%u", QN_MAX_QUBITS);
                    goto fail;
                }
                s.as.qreg.width = (uint32_t)width->int_value;
            }
            if (match(&p, TOK_ASSIGN)) {
                const QNToken *state = expect(&p, TOK_STATE, "basis state");
                if (!state) goto fail;
                snprintf(s.as.qreg.state_text, sizeof(s.as.qreg.state_text),
                         "%s", state->text);
                if (!parse_state(state->text, s.as.qreg.width,
                                 &s.as.qreg.initial_basis, diag,
                                 state->line, state->column)) goto fail;
            } else {
                snprintf(s.as.qreg.state_text, sizeof(s.as.qreg.state_text), "|0>");
                s.as.qreg.initial_basis = 0u;
            }
        } else if (match(&p, TOK_H) || match(&p, TOK_X) || match(&p, TOK_Z)) {
            QNTokenKind k = prev(&p)->kind;
            s.kind = k == TOK_H ? STMT_H : (k == TOK_X ? STMT_X : STMT_Z);
            if (!parse_target(&p, &s.as.unary.target)) goto fail;
        } else if (match(&p, TOK_CX)) {
            s.kind = STMT_CX;
            if (!parse_target(&p, &s.as.cx.control) ||
                !parse_target(&p, &s.as.cx.target)) goto fail;
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
            snprintf(s.as.measure.output, sizeof(s.as.measure.output),
                     "%s", output->text);
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
        } else if (match(&p, TOK_CALL)) {
            if (!parse_call_after_keyword(&p, &s)) goto fail;
        } else if (match(&p, TOK_RETURN)) {
            qn_diag_set_code(diag, "QN-E7582", start->line, start->column,
                             "return is legal only inside a Step6 function");
            goto fail;
        } else if (match(&p, TOK_IF)) {
            if (!parse_if_after_keyword(&p, &s)) goto fail;
        } else if (match(&p, TOK_REPEAT)) {
            if (seen_repeat) {
                qn_diag_set_code(diag, "QN-E7551", start->line, start->column,
                                 "Stage 7 Step 5 permits exactly one top-level repeat block");
                goto fail;
            }
            seen_repeat = true;
            if (!parse_repeat_after_keyword(&p, &s)) goto fail;
        } else if (match(&p, TOK_SET)) {
            qn_diag_set_code(diag, "QN-E7558", start->line, start->column,
                             "set is legal only inside a repeat block");
            goto fail;
        } else if (is(&p, TOK_IDENT)) {
            if (!parse_scalar_binary(&p, &s)) goto fail;
        } else if (match(&p, TOK_VECTOR_ADD_U32)) {
            s.kind = STMT_VECTOR_ADD_U32;
            if (!expect(&p, TOK_ARROW, "'->'")) goto fail;
            const QNToken *output = expect(&p, TOK_IDENT, "vector result name");
            if (!output) goto fail;
            snprintf(s.as.vector_add_u32.output,
                     sizeof(s.as.vector_add_u32.output), "%s", output->text);
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
