#ifndef QN_LEXER_H
#define QN_LEXER_H

#include "qn.h"

typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_IDENT,
    TOK_INT,
    TOK_STATE,
    TOK_ASSIGN,
    TOK_ARROW,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_DOT,

    TOK_QBIT,
    TOK_QREG,
    TOK_H,
    TOK_X,
    TOK_Z,
    TOK_CX,
    TOK_GHZ,
    TOK_MEASURE,
    TOK_EMIT,
    TOK_REQUIRES,
    TOK_SEED,
    TOK_SHOTS
} QNTokenKind;

typedef struct {
    QNTokenKind kind;
    int line;
    int column;
    uint64_t int_value;
    char text[QN_NAME_CAP];
} QNToken;

typedef struct {
    QNToken *items;
    size_t count;
    size_t capacity;
} QNTokenList;

void qn_tokens_free(QNTokenList *tokens);
const char *qn_token_kind_name(QNTokenKind kind);
QNStatus qn_lex(const char *source, QNTokenList *out, QNDiagnostic *diag);

#endif
