// std_template.c - builds get_std_header as AST; use -M to emit C.
// Usage (via script): ./generate_std.sh

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comptime
char *read_header_file(const char *path) {
    void *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, 2);
    long long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    fclose(f);
    buf[size] = 0;
    return buf;
}

#pragma macro
void generate_std_header(void) {
    const char *headers[] = {
        "Availability.h", "assert.h", "ctype.h", "errno.h",
        "float.h", "inttypes.h", "limits.h", "math.h", "reflection.h",
        "setjmp.h", "stdalign.h", "stdarg.h", "stdatomic.h", "stdbool.h",
        "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "stdnoreturn.h",
        "string.h", "sys/cdefs.h", "time.h",
        NULL
    };

    _Type *char_ptr_ty = _AST_MAKE_POINTER(_AST_GET_TYPE("char"));
    _Obj *fn = _AST_FUNCTION("get_std_header", char_ptr_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "filename", char_ptr_ty);

    _Node **stmts = malloc(24 * sizeof(_Node *));
    int n = 0;

    for (int i = 0; headers[i]; i++) {
        char path[256];
        snprintf(path, sizeof(path), "include/%s", headers[i]);
        char *content = read_header_file(path);
        if (!content) continue;

        _Node *strcmp_args[2] = {
            _AST_PARAM_REF(fn, "filename"),
            _AST_STRING_LITERAL(headers[i])
        };
        _Node *cmp  = _AST_FUNCALL(_AST_VAR_REF("strcmp"), strcmp_args, 2);
        _Node *cond = _AST_BINARY(_EQ, cmp, _AST_INT_LITERAL(0));
        _Node *ret  = _AST_RETURN(_AST_CAST(_AST_STRING_LITERAL(content), char_ptr_ty));

        stmts[n++] = _AST_IF(cond, ret, NULL);
    }

    stmts[n++] = _AST_RETURN(_AST_INT_LITERAL(0));

    _AST_FUNCTION_SET_BODY(fn, _AST_BLOCK(stmts, n));
    _AST_FORWARD_DECLARE(fn);
}

generate_std_header();
