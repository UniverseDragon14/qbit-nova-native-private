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
    STMT_SHOTS
} QNStmtKind;

typedef struct {
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
    } as;
} QNStmt;

typedef struct {
    QNStmt *items;
    size_t count;
    size_t capacity;
} QNProgram;

void qn_program_free(QNProgram *program);

#endif
