// std_template_test.c - builds get_std_header as AST; use -G to emit C.
// Usage: make stdlib

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include_comptime <glob.h>

[[jcc::comptime]]
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
[[jcc::comptime]]
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
[[jcc::comptime]]
const char *header_source_path(const char *header) {
    if (strcmp(header, "reflection.h") == 0)
        return "src/reflection.h";
    static char path[256];
    snprintf(path, sizeof(path), "include/%s", header);
    return path;
}

[[jcc::comptime]]
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

[[jcc::comptime]]
int header_name_less(const char *a, const char *b) {
    return strcmp(a, b) < 0;
}

[[jcc::comptime]]
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

[[jcc::comptime]]
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
[[jcc::comptime]]
$node_t *make_strcmp_return($obj_t *fn, $type_t *char_ptr_ty, const char *header) {
    char *gname = make_global_name(header);
    $node_t *args[2] = {
        $param_ref(fn, "filename"),
        $string_literal(header)
    };
    $node_t *cmp  = $funcall($var_ref("strcmp"), args, 2);
    $node_t *cond = $binary(nk_eq, cmp, $int_literal(0));
    $node_t *ret  = $return($cast($var_ref(gname), char_ptr_ty));
    return $if(cond, ret, NULL);
}

struct reg_entry { char *header; char *fn; };

// Map header name to its stdlib registration function name.
// Returns NULL for headers that don't need runtime registration.
[[jcc::comptime]]
const char *reg_fn_for_header(const char *header) {
    static struct reg_entry *map = NULL;
    static int n = 0;
    if (!map) {
        static const char raw[] = {
            #embed "stdlib.tsv"
            , 0
        };
        const char *p = raw;
        while (*p) {
            const char *nl = strchr(p, '\n');
            if (!nl) break;
            const char *tab = strchr(p, '\t');
            if (tab && tab < nl) {
                int hlen = (int)(tab - p);
                int flen = (int)(nl - tab - 1);
                map = realloc(map, (n + 1) * sizeof(struct reg_entry));
                map[n].header = malloc(hlen + 1);
                memcpy(map[n].header, p, hlen);
                map[n].header[hlen] = 0;
                map[n].fn = malloc(flen + 1);
                memcpy(map[n].fn, tab + 1, flen);
                map[n].fn[flen] = 0;
                n++;
            }
            p = nl + 1;
        }
    }
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(header, map[mid].header);
        if (cmp == 0)
            return map[mid].fn;
        else if (cmp < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

// Build: if (strcmp(header, name) == 0) return "register_XXX";
[[jcc::comptime]]
$node_t *make_strcmp_return_fn_name($obj_t *fn, const char *header, const char *fn_name) {
    $node_t *args[2] = {
        $param_ref(fn, "header"),
        $string_literal(header)
    };
    $node_t *cmp  = $funcall($var_ref("strcmp"), args, 2);
    $node_t *cond = $binary(nk_eq, cmp, $int_literal(0));
    return $if(cond, $return($string_literal(fn_name)), NULL);
}

[[jcc::macro]]
void generate_stdlib_reg_fn(void) {
    $forward_include("<string.h>");

    char **headers = discover_headers();
    if (!headers) return;

    $type_t *char_ptr_ty = $make_pointer($get_type("char"));
    $obj_t  *fn          = $function("get_stdlib_reg_fn_name", char_ptr_ty);
    $function_add_param(fn, "header", char_ptr_ty);

    $with_fn(fn) {
        // Filter to only headers that have a registration function
        int total = 0;
        for (int i = 0; headers[i]; i++) {
            if (reg_fn_for_header(headers[i])) total++;
        }
        if (!total) {
            $function_set_body(fn, $return($int_literal(0)));
            $publish(fn);
            return;
        }

        $node_t *first_char = $unary(nk_deref, $param_ref(fn, "header"));
        $node_t *sw = $switch(first_char);

        unsigned char seen[256] = {0};
        for (int i = 0; headers[i]; i++) {
            unsigned char c = (unsigned char)headers[i][0];
            if (seen[c]) continue;
            seen[c] = 1;

            int bucket_size = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] == c && reg_fn_for_header(headers[j]))
                    bucket_size++;
            }
            if (!bucket_size) continue;

            $node_t **case_stmts = malloc((bucket_size + 1) * sizeof($node_t *));
            int cn = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] != c) continue;
                const char *fn_name = reg_fn_for_header(headers[j]);
                if (!fn_name) continue;
                case_stmts[cn++] = make_strcmp_return_fn_name(fn, headers[j], fn_name);
            }
            case_stmts[cn++] = $return($int_literal(0));

            $node_t *case_body = $block(case_stmts, cn);
            $switch_add_case(sw, $int_literal(c), case_body);
        }

        $node_t **stmts = malloc(3 * sizeof($node_t *));
        int n = 0;
        stmts[n++] = sw;
        stmts[n++] = $return($int_literal(0));
        $function_set_body(fn, $block(stmts, n));
    }

    $publish(fn);
}

[[jcc::macro]]
void generate_std_header(void) {
    $forward_include("<string.h>");

    char **headers = discover_headers();
    if (!headers)
        return;

    $type_t *char_ty     = $get_type("char");
    $type_t *char_ptr_ty = $make_pointer(char_ty);
    $obj_t  *fn          = $function("get_std_header", char_ptr_ty);
    $function_add_param(fn, "filename", char_ptr_ty);

    $with_fn(fn) {
        // Count total headers that exist on disk and emit their globals.
        int total = 0;
        for (int i = 0; headers[i]; i++) {
            char *content = read_header_file(header_source_path(headers[i]));
            if (!content) continue;

            int content_len = (int)strlen(content) + 1;
            char *gname   = make_global_name(headers[i]);
            $type_t *arr_ty = $make_array(char_ty, content_len);
            $obj_t  *gvar   = $global_var(gname, arr_ty);
            $global_var_set_init_data(gvar, content, content_len);
            $global_var_set_static(gvar, 1);
            total++;
        }

        // Build a trie dispatching on filename[0] (first char of the header
        // name). Within each case, sequential strcmp checks are used. Since
        // most first-char buckets are small (1-3 headers), this reduces the
        // average number of comparisons from O(N) to O(bucket_size).
        $node_t *first_char = $unary(nk_deref, $param_ref(fn, "filename"));
        $node_t *sw = $switch(first_char);

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
            $node_t **case_stmts = malloc((bucket_size + 1) * sizeof($node_t *));
            int cn = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] != c) continue;
                if (!read_header_file(header_source_path(headers[j]))) continue;
                case_stmts[cn++] = make_strcmp_return(fn, char_ptr_ty, headers[j]);
            }
            case_stmts[cn++] = $return($int_literal(0));

            $node_t *case_body = $block(case_stmts, cn);
            $switch_add_case(sw, $int_literal(c), case_body);
        }

        $node_t **stmts = malloc(3 * sizeof($node_t *));
        int n = 0;
        stmts[n++] = sw;
        stmts[n++] = $return($int_literal(0));
        $function_set_body(fn, $block(stmts, n));
    }

    $publish(fn);
}

generate_std_header();
generate_stdlib_reg_fn();
