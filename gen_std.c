// std_template_test.c - builds get_std_header as AST; use -G to emit C.
// Usage: make generate-std

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

// Sanitize a header filename into a valid C identifier, e.g.
// "sys/cdefs.h" -> "__jcc_std_sys_cdefs_h"
#pragma comptime
char *make_global_name(const char *header) {
    char *buf = malloc(strlen(header) + 12);
    memcpy(buf, "__jcc_std_", 10);
    char *dst = buf + 10;
    for (const char *src = header; *src; src++, dst++)
        *dst = (*src == '.' || *src == '/') ? '_' : *src;
    *dst = '\0';
    return buf;
}

#pragma macro
void generate_std_header(void) {
    const char *headers[] = {
        "Availability.h", "assert.h", "ctype.h", "errno.h",
        "complex.h", "dlfcn.h", "fcntl.h", "fenv.h", "float.h", "inttypes.h", "iso646.h",
        "limits.h", "locale.h", "math.h", "reflection.h",
        "setjmp.h", "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h", "stdbool.h",
        "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "stdnoreturn.h",
        "string.h", "sys/cdefs.h", "tgmath.h", "time.h", "uchar.h",
        "unistd.h", "wchar.h", "wctype.h",
        NULL
    };

    _Type *char_ty     = _AST_GET_TYPE("char");
    _Type *char_ptr_ty = _AST_MAKE_POINTER(char_ty);
    _Obj  *fn          = _AST_FUNCTION("get_std_header", char_ptr_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "filename", char_ptr_ty);

    _Node **stmts = malloc(24 * sizeof(_Node *));
    int n = 0;

    _AST_WITH_FN(fn) {
        for (int i = 0; headers[i]; i++) {
            char path[256];
            snprintf(path, sizeof(path), "include/%s", headers[i]);
            char *content = read_header_file(path);
            if (!content) continue;

            // Emit a static global holding this header's content.
            int content_len = (int)strlen(content) + 1;
            char *gname   = make_global_name(headers[i]);
            _Type *arr_ty = _AST_MAKE_ARRAY(char_ty, content_len);
            _Obj  *gvar   = _AST_GLOBAL_VAR(gname, arr_ty);
            _AST_GLOBAL_VAR_SET_INIT_DATA(gvar, content, content_len);
            _AST_GLOBAL_VAR_SET_STATIC(gvar, 1);

            // Build: if (strcmp(filename, "X.h") == 0) return __jcc_std_X_h;
            _Node *strcmp_args[2] = {
                _AST_PARAM_REF(fn, "filename"),
                _AST_STRING_LITERAL(headers[i])
            };
            _Node *cmp  = _AST_FUNCALL(_AST_VAR_REF("strcmp"), strcmp_args, 2);
            _Node *cond = _AST_BINARY(_EQ, cmp, _AST_INT_LITERAL(0));
            _Node *ret  = _AST_RETURN(_AST_CAST(_AST_VAR_REF(gname), char_ptr_ty));

            stmts[n++] = _AST_IF(cond, ret, NULL);
        }

        stmts[n++] = _AST_RETURN(_AST_INT_LITERAL(0));
    }

    _AST_FUNCTION_SET_BODY(fn, _AST_BLOCK(stmts, n));
    _AST_FORWARD_DECLARE(fn);
}

generate_std_header();
