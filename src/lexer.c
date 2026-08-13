#include "qn_lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool push(QNTokenList *list, QNToken token, QNDiagnostic *diag) {
    if (list->count >= QN_MAX_TOKENS) {
        qn_diag_set(diag, token.line, token.column, "token limit exceeded");
        return false;
    }
    if (list->count == list->capacity) {
        size_t cap = list->capacity ? list->capacity * 2 : 256;
        QNToken *next = realloc(list->items, cap * sizeof(*next));
        if (!next) {
            qn_diag_set(diag, token.line, token.column, "out of memory");
            return false;
        }
        list->items = next;
        list->capacity = cap;
    }
    list->items[list->count++] = token;
    return true;
}

void qn_tokens_free(QNTokenList *tokens) {
    if (!tokens) return;
    free(tokens->items);
    memset(tokens, 0, sizeof(*tokens));
}

const char *qn_token_kind_name(QNTokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_NEWLINE: return "NEWLINE";
        case TOK_IDENT: return "IDENT";
        case TOK_INT: return "INT";
        case TOK_STATE: return "STATE";
        case TOK_ASSIGN: return "ASSIGN";
        case TOK_EQUAL: return "EQUAL";
        case TOK_ARROW: return "ARROW";
        case TOK_LBRACKET: return "LBRACKET";
        case TOK_RBRACKET: return "RBRACKET";
        case TOK_DOT: return "DOT";
        case TOK_COLON: return "COLON";
        case TOK_PLUS: return "PLUS";
        case TOK_MINUS: return "MINUS";
        case TOK_STAR: return "STAR";
        case TOK_SLASH: return "SLASH";
        case TOK_EQ_EQ: return "EQ_EQ";
        case TOK_BANG_EQUAL: return "BANG_EQUAL";
        case TOK_LT: return "LT";
        case TOK_LT_EQUAL: return "LT_EQUAL";
        case TOK_GT: return "GT";
        case TOK_GT_EQUAL: return "GT_EQUAL";
        case TOK_LBRACE: return "LBRACE";
        case TOK_RBRACE: return "RBRACE";
        case TOK_LPAREN: return "LPAREN";
        case TOK_RPAREN: return "RPAREN";
        case TOK_COMMA: return "COMMA";
        case TOK_QBIT: return "QBIT";
        case TOK_QREG: return "QREG";
        case TOK_H: return "H";
        case TOK_X: return "X";
        case TOK_Z: return "Z";
        case TOK_CX: return "CX";
        case TOK_GHZ: return "GHZ";
        case TOK_MEASURE: return "MEASURE";
        case TOK_EMIT: return "EMIT";
        case TOK_REQUIRES: return "REQUIRES";
        case TOK_SEED: return "SEED";
        case TOK_SHOTS: return "SHOTS";
        case TOK_VECTOR_ADD_U32: return "VECTOR_ADD_U32";
        case TOK_LET: return "LET";
        case TOK_U32: return "U32";
        case TOK_IF: return "IF";
        case TOK_ELSE: return "ELSE";
        case TOK_REPEAT: return "REPEAT";
        case TOK_SET: return "SET";
        case TOK_FN: return "FN";
        case TOK_CALL: return "CALL";
        case TOK_RETURN: return "RETURN";
        default: return "UNKNOWN";
    }
}

static QNTokenKind keyword(const char *s) {
    if (!strcmp(s, "qbit")) return TOK_QBIT;
    if (!strcmp(s, "qreg")) return TOK_QREG;
    if (!strcmp(s, "h")) return TOK_H;
    if (!strcmp(s, "x")) return TOK_X;
    if (!strcmp(s, "z")) return TOK_Z;
    if (!strcmp(s, "cx") || !strcmp(s, "cnot")) return TOK_CX;
    if (!strcmp(s, "ghz")) return TOK_GHZ;
    if (!strcmp(s, "measure") || !strcmp(s, "m")) return TOK_MEASURE;
    if (!strcmp(s, "emit")) return TOK_EMIT;
    if (!strcmp(s, "requires")) return TOK_REQUIRES;
    if (!strcmp(s, "seed")) return TOK_SEED;
    if (!strcmp(s, "shots")) return TOK_SHOTS;
    if (!strcmp(s, "vector_add_u32") ||
        !strcmp(s, "u32_vector_add")) return TOK_VECTOR_ADD_U32;
    if (!strcmp(s, "let")) return TOK_LET;
    if (!strcmp(s, "u32")) return TOK_U32;
    if (!strcmp(s, "if")) return TOK_IF;
    if (!strcmp(s, "else")) return TOK_ELSE;
    if (!strcmp(s, "repeat")) return TOK_REPEAT;
    if (!strcmp(s, "set")) return TOK_SET;
    if (!strcmp(s, "fn")) return TOK_FN;
    if (!strcmp(s, "call")) return TOK_CALL;
    if (!strcmp(s, "return")) return TOK_RETURN;
    return TOK_IDENT;
}

QNStatus qn_lex(const char *source, QNTokenList *out, QNDiagnostic *diag) {
    memset(out, 0, sizeof(*out));
    int line = 1, col = 1;
    size_t i = 0;

    while (source[i]) {
        char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r') { ++i; ++col; continue; }
        if (c == '\n' || c == ';') {
            QNToken t = {.kind=TOK_NEWLINE,.line=line,.column=col};
            if (!push(out, t, diag)) goto fail;
            ++i;
            if (c == '\n') { ++line; col = 1; } else ++col;
            continue;
        }
        if (c == '#') {
            while (source[i] && source[i] != '\n') { ++i; ++col; }
            continue;
        }
        if (c == '/' && source[i+1] == '/') {
            i += 2; col += 2;
            while (source[i] && source[i] != '\n') { ++i; ++col; }
            continue;
        }

        QNToken t = {.line=line,.column=col};

        if (c == ':' && source[i+1] == '=') {
            t.kind = TOK_ASSIGN; i += 2; col += 2;
        } else if (c == '=' && source[i+1] == '=') {
            t.kind = TOK_EQ_EQ; i += 2; col += 2;
        } else if (c == '!' && source[i+1] == '=') {
            t.kind = TOK_BANG_EQUAL; i += 2; col += 2;
        } else if (c == '<' && source[i+1] == '=') {
            t.kind = TOK_LT_EQUAL; i += 2; col += 2;
        } else if (c == '>' && source[i+1] == '=') {
            t.kind = TOK_GT_EQUAL; i += 2; col += 2;
        } else if (c == '=') {
            t.kind = TOK_EQUAL; ++i; ++col;
        } else if (c == '<') {
            t.kind = TOK_LT; ++i; ++col;
        } else if (c == '>') {
            t.kind = TOK_GT; ++i; ++col;
        } else if (c == '-' && source[i+1] == '>') {
            t.kind = TOK_ARROW; i += 2; col += 2;
        } else if (c == '[') {
            t.kind = TOK_LBRACKET; ++i; ++col;
        } else if (c == ']') {
            t.kind = TOK_RBRACKET; ++i; ++col;
        } else if (c == '{') {
            t.kind = TOK_LBRACE; ++i; ++col;
        } else if (c == '}') {
            t.kind = TOK_RBRACE; ++i; ++col;
        } else if (c == '(') {
            t.kind = TOK_LPAREN; ++i; ++col;
        } else if (c == ')') {
            t.kind = TOK_RPAREN; ++i; ++col;
        } else if (c == ',') {
            t.kind = TOK_COMMA; ++i; ++col;
        } else if (c == '.') {
            t.kind = TOK_DOT; ++i; ++col;
        } else if (c == ':') {
            t.kind = TOK_COLON; ++i; ++col;
        } else if (c == '+') {
            t.kind = TOK_PLUS; ++i; ++col;
        } else if (c == '-') {
            t.kind = TOK_MINUS; ++i; ++col;
        } else if (c == '*') {
            t.kind = TOK_STAR; ++i; ++col;
        } else if (c == '/') {
            t.kind = TOK_SLASH; ++i; ++col;
        } else if (c == '|') {
            size_t start = i++;
            int start_col = col++;
            size_t bits = 0;
            while (source[i] == '0' || source[i] == '1') { ++i; ++col; ++bits; }
            if (!bits || source[i] != '>') {
                qn_diag_set(diag, line, start_col, "invalid basis state; expected |010...>");
                goto fail;
            }
            ++i; ++col;
            size_t len = i - start;
            if (len >= sizeof(t.text)) {
                qn_diag_set(diag, line, start_col, "basis state is too long");
                goto fail;
            }
            memcpy(t.text, source + start, len);
            t.text[len] = 0;
            t.kind = TOK_STATE;
        } else if (isdigit((unsigned char)c)) {
            uint64_t value = 0;
            size_t start = i;
            while (isdigit((unsigned char)source[i])) {
                unsigned digit = (unsigned)(source[i] - '0');
                if (value > (UINT64_MAX - digit) / 10u) {
                    qn_diag_set(diag, line, col, "integer overflow");
                    goto fail;
                }
                value = value * 10u + digit;
                ++i; ++col;
            }
            size_t len = i - start;
            if (len >= sizeof(t.text)) len = sizeof(t.text)-1;
            memcpy(t.text, source+start, len); t.text[len]=0;
            t.kind = TOK_INT; t.int_value = value;
        } else if (isalpha((unsigned char)c) || c == '_') {
            size_t start = i;
            while (isalnum((unsigned char)source[i]) || source[i] == '_') { ++i; ++col; }
            size_t len = i - start;
            if (len >= sizeof(t.text)) {
                qn_diag_set(diag, line, t.column, "identifier exceeds %u characters", QN_NAME_CAP-1);
                goto fail;
            }
            for (size_t k = 0; k < len; ++k) t.text[k] = (char)tolower((unsigned char)source[start+k]);
            t.text[len] = 0;
            t.kind = keyword(t.text);
        } else {
            qn_diag_set(diag, line, col, "unexpected character '%c'", c);
            goto fail;
        }

        if (!push(out, t, diag)) goto fail;
    }

    {
        QNToken eof = {.kind=TOK_EOF,.line=line,.column=col};
        if (!push(out, eof, diag)) goto fail;
    }
    return QN_OK;

fail:
    qn_tokens_free(out);
    return QN_ERR_LEX;
}
