// std_template_test.c - builds get_std_header as AST; use -G to emit C.
// Usage: make generate-std

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>

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

// reflection.h is private to pragma macros. Keep embedding it, but do not
// keep it under include/ where user source can find it as a JCC header.
#pragma comptime
const char *header_source_path(const char *header) {
    if (strcmp(header, "reflection.h") == 0)
        return "src/reflection.h";
    static char path[256];
    snprintf(path, sizeof(path), "include/%s", header);
    return path;
}

#pragma comptime
char *copy_header_name(const char *path) {
    const char *prefix = "include/";
    int prefix_len = (int)strlen(prefix);
    if (strncmp(path, prefix, prefix_len) == 0)
        path += prefix_len;

    int len = (int)strlen(path);
    char *copy = malloc(len + 1);
    memcpy(copy, path, len + 1);
    return copy;
}

#pragma comptime
int header_name_less(const char *a, const char *b) {
    return strcmp(a, b) < 0;
}

#pragma comptime
void sort_headers(char **headers, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (header_name_less(headers[j], headers[i])) {
                char *tmp = headers[i];
                headers[i] = headers[j];
                headers[j] = tmp;
            }
        }
    }
}

#pragma comptime
char **discover_headers(void) {
    glob_t g;
    memset(&g, 0, sizeof(g));

    int rc = glob("include/*.h", 0, NULL, &g);
    if (rc != 0 && rc != GLOB_NOMATCH)
        return NULL;

    rc = glob("include/*/*.h", GLOB_APPEND, NULL, &g);
    if (rc != 0 && rc != GLOB_NOMATCH) {
        globfree(&g);
        return NULL;
    }

    int count = (int)g.gl_pathc + 1; // plus reflection.h
    char **headers = malloc((count + 1) * sizeof(char *));
    int n = 0;
    headers[n++] = copy_header_name("reflection.h");
    for (int i = 0; i < (int)g.gl_pathc; i++)
        headers[n++] = copy_header_name(g.gl_pathv[i]);
    headers[n] = NULL;

    sort_headers(headers, n);
    globfree(&g);
    return headers;
}

// Build: if (strcmp(filename, header) == 0) return __jcc_std_XXX;
#pragma comptime
_Node *make_strcmp_return(_Obj *fn, _Type *char_ptr_ty, const char *header) {
    char *gname = make_global_name(header);
    _Node *args[2] = {
        _AST_PARAM_REF(fn, "filename"),
        _AST_STRING_LITERAL(header)
    };
    _Node *cmp  = _AST_FUNCALL(_AST_VAR_REF("strcmp"), args, 2);
    _Node *cond = _AST_BINARY(_EQ, cmp, _AST_INT_LITERAL(0));
    _Node *ret  = _AST_RETURN(_AST_CAST(_AST_VAR_REF(gname), char_ptr_ty));
    return _AST_IF(cond, ret, NULL);
}

#pragma macro
void generate_std_header(void) {
    char **headers = discover_headers();
    if (!headers)
        return;

    _Type *char_ty     = _AST_GET_TYPE("char");
    _Type *char_ptr_ty = _AST_MAKE_POINTER(char_ty);
    _Obj  *fn          = _AST_FUNCTION("get_std_header", char_ptr_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "filename", char_ptr_ty);

    _AST_WITH_FN(fn) {
        // Count total headers that exist on disk and emit their globals.
        int total = 0;
        for (int i = 0; headers[i]; i++) {
            char *content = read_header_file(header_source_path(headers[i]));
            if (!content) continue;

            int content_len = (int)strlen(content) + 1;
            char *gname   = make_global_name(headers[i]);
            _Type *arr_ty = _AST_MAKE_ARRAY(char_ty, content_len);
            _Obj  *gvar   = _AST_GLOBAL_VAR(gname, arr_ty);
            _AST_GLOBAL_VAR_SET_INIT_DATA(gvar, content, content_len);
            _AST_GLOBAL_VAR_SET_STATIC(gvar, 1);
            total++;
        }

        // Build a trie dispatching on filename[0] (first char of the header
        // name). Within each case, sequential strcmp checks are used. Since
        // most first-char buckets are small (1-3 headers), this reduces the
        // average number of comparisons from O(N) to O(bucket_size).
        _Node *first_char = _AST_UNARY(_DEREF, _AST_PARAM_REF(fn, "filename"));
        _Node *sw = _AST_SWITCH(first_char);

        // Find each unique first character and build its case.
        unsigned char seen[256] = {0};
        for (int i = 0; headers[i]; i++) {
            unsigned char c = (unsigned char)headers[i][0];
            if (seen[c]) continue;
            seen[c] = 1;

            // Count headers in this bucket.
            int bucket_size = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] == c) {
                    if (read_header_file(header_source_path(headers[j]))) bucket_size++;
                }
            }
            if (bucket_size == 0) continue;

            // Build the case body: one strcmp-return per header in bucket,
            // then return NULL for non-matching names with this first char.
            _Node **case_stmts = malloc((bucket_size + 1) * sizeof(_Node *));
            int cn = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] != c) continue;
                if (!read_header_file(header_source_path(headers[j]))) continue;
                case_stmts[cn++] = make_strcmp_return(fn, char_ptr_ty, headers[j]);
            }
            case_stmts[cn++] = _AST_RETURN(_AST_INT_LITERAL(0));

            _Node *case_body = _AST_BLOCK(case_stmts, cn);
            _AST_SWITCH_ADD_CASE(sw, _AST_INT_LITERAL(c), case_body);
        }

        _Node **stmts = malloc(3 * sizeof(_Node *));
        int n = 0;
        stmts[n++] = sw;
        stmts[n++] = _AST_RETURN(_AST_INT_LITERAL(0));
        _AST_FUNCTION_SET_BODY(fn, _AST_BLOCK(stmts, n));
    }

    _AST_FORWARD_DECLARE(fn);
}

generate_std_header();
