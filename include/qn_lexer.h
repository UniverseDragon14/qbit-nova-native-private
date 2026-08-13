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
    TOK_EQUAL,
    TOK_ARROW,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_DOT,
    TOK_COLON,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ_EQ,
    TOK_BANG_EQUAL,
    TOK_LT,
    TOK_LT_EQUAL,
    TOK_GT,
    TOK_GT_EQUAL,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,

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
    TOK_SHOTS,
    TOK_VECTOR_ADD_U32,
    TOK_LET,
    TOK_U32,
    TOK_IF,
    TOK_ELSE,
    TOK_REPEAT,
    TOK_SET,
    TOK_FN,
    TOK_CALL,
    TOK_RETURN
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
