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
    STMT_REPEAT,
    STMT_CALL,
    STMT_RETURN,
    STMT_TENSOR_DECL,
    STMT_DEVICE_GPIO,
    STMT_DEVICE_WRITE
} QNStmtKind;

typedef enum {
    QN_TENSOR_ELEMENT_F32 = 1,
    QN_TENSOR_ELEMENT_I8 = 2
} QNTensorElementType;

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
            char name[QN_NAME_CAP];
            QNTensorElementType element_type;
            uint32_t element_count;
        } tensor_decl;
        struct {
            char name[QN_NAME_CAP];
            uint32_t line_offset;
        } device_gpio;
        struct {
            char name[QN_NAME_CAP];
            bool high;
        } device_write;
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
        struct {
            char function[QN_NAME_CAP];
            char args[QN_MAX_FUNCTION_PARAMS][QN_NAME_CAP];
            uint8_t arg_count;
            char output[QN_NAME_CAP];
        } call;
        struct { char name[QN_NAME_CAP]; } return_stmt;
    } as;
};

typedef struct {
    char name[QN_NAME_CAP];
    char params[QN_MAX_FUNCTION_PARAMS][QN_NAME_CAP];
    uint8_t param_count;
    QNStmt *body_items;
    size_t body_count;
    int line;
    int column;
} QNFunctionDecl;

typedef struct {
    char name[QN_NAME_CAP];
    uint8_t name_sha256[32];
    int line;
    int column;
} QNInputDecl;

typedef struct {
    QNFunctionDecl functions[QN_MAX_FUNCTIONS];
    size_t function_count;
    QNInputDecl inputs[QN_MAX_RUNTIME_INPUTS];
    size_t input_count;
    QNStmt *items;
    size_t count;
    size_t capacity;
} QNProgram;

void qn_program_free(QNProgram *program);

#endif
