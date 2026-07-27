#ifndef QN_PARSER_H
#define QN_PARSER_H

#include "qn_lexer.h"
#include "qn_ast.h"

QNStatus qn_parse(const QNTokenList *tokens, QNProgram *out, QNDiagnostic *diag);

#endif
