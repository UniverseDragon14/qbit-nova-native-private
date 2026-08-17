#include "qn_v10_data.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void qn_diag_set(QNDiagnostic *diag, int line, int column, const char *fmt, ...) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
    diag->line = line;
    diag->column = column;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(diag->message, sizeof(diag->message), fmt, ap);
    va_end(ap);
}

void qn_diag_set_code(QNDiagnostic *diag, const char *code,
                      int line, int column, const char *fmt, ...) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
    diag->line = line;
    diag->column = column;
    if (code) snprintf(diag->code, sizeof(diag->code), "%s", code);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(diag->message, sizeof(diag->message), fmt, ap);
    va_end(ap);
}

int main(void) {
    const char *source =
        "let gain: f32 = 0.75\n"
        "let greeting: string = \"Vanakkam Aslam\"\n"
        "let tamil: string = \"வணக்கம் Aslam\"\n"
        "let packet: bytes = b\"QBIT\\x00NOVA\"\n";
    QNDiagnostic diag = {0};
    QNV10DataProgram ast = {0};
    assert(qn_v10_data_parse_source(source, &ast, &diag) == QN_OK);
    assert(ast.count == 4u);
    assert(ast.declarations[0].kind == QN_VALUE_F32);
    assert(fabsf(ast.declarations[0].as.f32 - 0.75f) < 0.0001f);
    assert(ast.declarations[1].kind == QN_VALUE_STRING);
    assert(ast.declarations[1].as.blob.byte_length == 14u);
    assert(ast.declarations[2].kind == QN_VALUE_STRING);
    assert(ast.declarations[3].kind == QN_VALUE_BYTES);
    assert(ast.declarations[3].as.blob.byte_length == 9u);
    assert(ast.declarations[3].as.blob.data[4] == 0u);

    QNV10DataQIRProgram qir = {0};
    assert(qn_v10_data_qir_build(&ast, &qir, &diag) == QN_OK);
    assert(qir.abi_version == QN_V10_DATA_ABI_V1);
    assert(qir.value_count == 4u);
    assert(qir.requires_qbc_v10);
    assert(qir.values[0].kind == QN_VALUE_F32);
    assert(qir.values[1].kind == QN_VALUE_STRING);
    assert(qir.values[3].kind == QN_VALUE_BYTES);
    assert(qn_v10_data_qbc_guard(&qir, &diag) == QN_ERR_QBC);
    assert(strcmp(diag.code, "QN-E7818") == 0);
    qn_v10_data_qir_free(&qir);
    qn_v10_data_program_free(&ast);

    const char *duplicate =
        "let x: f32 = 1.0\n"
        "let x: string = \"bad\"\n";
    memset(&diag, 0, sizeof(diag));
    assert(qn_v10_data_parse_source(duplicate, &ast, &diag) == QN_ERR_SEMANTIC);
    assert(strcmp(diag.code, "QN-E7813") == 0);

    const char malformed_utf8[] = {
        'l','e','t',' ','x',':',' ','s','t','r','i','n','g',' ','=',' ','"',
        (char)0xc0,(char)0xaf,'"','\n','\0'
    };
    memset(&diag, 0, sizeof(diag));
    assert(qn_v10_data_parse_source(malformed_utf8, &ast, &diag) == QN_ERR_SEMANTIC);
    assert(strcmp(diag.code, "QN-E7802") == 0);

    memset(&diag, 0, sizeof(diag));
    assert(qn_v10_data_parse_source("let x: f32 = 1e50\n", &ast, &diag) == QN_ERR_LEX);
    assert(strcmp(diag.code, "QN-E7811") == 0);

    memset(&diag, 0, sizeof(diag));
    assert(qn_v10_data_parse_source("let x: f32 = \"no\"\n", &ast, &diag) == QN_ERR_PARSE);
    assert(strcmp(diag.code, "QN-E7814") == 0);

    puts("QBIT_NOVA_V10_NATIVE_DATA_STEP2A=PASS");
    puts("V10_SOURCE_LEXER=PASS");
    puts("V10_TYPED_AST=PASS");
    puts("V10_SEMANTIC_VALIDATION=PASS");
    puts("V10_TYPED_QIR=PASS");
    puts("V10_QBC_LEGACY_FAIL_CLOSED=PASS");
    puts("V10_F32_LITERAL=PASS");
    puts("V10_UTF8_STRING_LITERAL=PASS");
    puts("V10_BYTES_LITERAL=PASS");
    return 0;
}
