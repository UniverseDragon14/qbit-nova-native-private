#ifndef QN_AST_H
#define QN_AST_H

#include "qn.h"

typedef struct {
    char reg[QN_NAME_CAP];
    bool has_index;
    uint32_t index;
    int line;
    int column;
} QNTarget;

typedef enum {
    STMT_QREG = 1,
    STMT_H,
    STMT_X,
    STMT_Z,
    STMT_CX,
    STMT_GHZ,
    STMT_MEASURE,
    STMT_EMIT,
    STMT_REQUIRES,
    STMT_SEED,
    STMT_SHOTS,
    STMT_VECTOR_ADD_U32,
    STMT_U32_LET,
    STMT_U32_ADD,
    STMT_U32_SUB,
    STMT_U32_MUL,
    STMT_U32_DIV,
    STMT_U32_EQ,
    STMT_U32_NE,
    STMT_U32_LT,
    STMT_U32_LE,
    STMT_U32_GT,
    STMT_U32_GE,
    STMT_IF,
    STMT_U32_SET_ADD,
    STMT_U32_SET_SUB,
    STMT_U32_SET_MUL,
    STMT_U32_SET_DIV,
    STMT_REPEAT
} QNStmtKind;

typedef struct QNStmt QNStmt;

struct QNStmt {
    QNStmtKind kind;
    int line;
    int column;
    union {
        struct {
            char name[QN_NAME_CAP];
            uint32_t width;
            uint64_t initial_basis;
            char state_text[QN_NAME_CAP];
        } qreg;
        struct { QNTarget target; } unary;
        struct { QNTarget control; QNTarget target; } cx;
        struct { char reg[QN_NAME_CAP]; } ghz;
        struct { char reg[QN_NAME_CAP]; char output[QN_NAME_CAP]; } measure;
        struct { char name[QN_NAME_CAP]; } emit;
        struct { char capability[QN_NAME_CAP]; } requires;
        struct { uint64_t value; } number;
        struct { char output[QN_NAME_CAP]; } vector_add_u32;
        struct { char name[QN_NAME_CAP]; uint32_t value; } u32_let;
        struct {
            char output[QN_NAME_CAP];
            char left[QN_NAME_CAP];
            char right[QN_NAME_CAP];
        } scalar_binary;
        struct {
            char condition[QN_NAME_CAP];
            QNStmt *then_items;
            size_t then_count;
            QNStmt *else_items;
            size_t else_count;
        } if_stmt;
        struct {
            uint32_t iterations;
            QNStmt *body_items;
            size_t body_count;
        } repeat_stmt;
    } as;
};

typedef struct {
    QNStmt *items;
    size_t count;
    size_t capacity;
} QNProgram;

void qn_program_free(QNProgram *program);

#endif
