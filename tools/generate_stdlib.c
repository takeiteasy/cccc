// generate_stdlib.c - builds get_std_header as AST; use -G to emit C.
// Usage: make bootstrap (or: sh tools/regen_stdlib.sh <cccc-binary>)

#include <string.h>

#pragma cccc comptime
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>

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
// "sys/cdefs.h" -> "__builtin_sys_cdefs_h"
// (#851: this comment previously claimed an "__builtin_std_" prefix, but the
// memcpy below only ever copies 10 of those 14 bytes -- the emitted prefix
// has always been "__builtin_". Harmless: the same function both generates
// the definition and every reference to it, so it's self-consistent; this is
// a documentation fix, not a behaviour change -- changing the emitted prefix
// would require also regenerating and re-committing src/std_seed.c.)
char *make_global_name(const char *header) {
    char *buf = malloc(strlen(header) + 12);
    memcpy(buf, "__builtin_std_", 10);
    char *dst = buf + 10;
    for (const char *src = header; *src; src++, dst++)
        *dst = (*src == '.' || *src == '/') ? '_' : *src;
    *dst = '\0';
    return buf;
}

// reflection.h, testing.h and building.h are private headers. Keep embedding
// them, but do not place them under include/ where user source can find them.
const char *header_source_path(const char *header) {
    if (strcmp(header, "reflection.h") == 0)
        return "include/cccc/reflection.h";
    if (strcmp(header, "testing.h") == 0)
        return "include/cccc/testing.h";
    if (strcmp(header, "building.h") == 0)
        return "include/cccc/building.h";
    static char path[256];
    snprintf(path, sizeof(path), "include/%s", header);
    return path;
}

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

int header_name_less(const char *a, const char *b) {
    return strcmp(a, b) < 0;
}

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

    // Count non-cccc results first (cccc/ private headers are added manually).
    int public_count = 0;
    for (int i = 0; i < (int)g.gl_pathc; i++) {
        const char *name = copy_header_name(g.gl_pathv[i]);
        int skip = (strncmp(name, "cccc/", 4) == 0);
        free((void *)name);
        if (!skip) public_count++;
    }

    int count = public_count + 3; // plus reflection.h, testing.h, building.h
    char **headers = malloc((count + 1) * sizeof(char *));
    int n = 0;
    headers[n++] = copy_header_name("reflection.h");
    headers[n++] = copy_header_name("testing.h");
    headers[n++] = copy_header_name("building.h");
    for (int i = 0; i < (int)g.gl_pathc; i++) {
        char *name = copy_header_name(g.gl_pathv[i]);
        if (strncmp(name, "cccc/", 4) == 0) {
            free(name);
            continue;
        }
        headers[n++] = name;
    }
    headers[n] = NULL;

    sort_headers(headers, n);
    globfree(&g);
    return headers;
}

// Build: if (strcmp(filename, header) == 0) return __builtin_std_XXX;
Node *make_strcmp_return(Obj *fn, Type *char_ptr_ty, const char *header) {
    char *gname = make_global_name(header);
    Node *args[2] = {
        MakeParamRef(fn, "filename"),
        MakeStringLiteral(header)
    };
    Node *cmp  = MakeFuncCall(MakeVarRef("strcmp"), args, 2);
    Node *cond = MakeBinary(NK_EQ, cmp, MakeIntLiteral(0));
    Node *ret  = MakeReturn(MakeCast(MakeVarRef(gname), char_ptr_ty));
    return MakeIf(cond, ret, NULL);
}

struct reg_entry { char *header; char *fn; };

// Map header name to its stdlib registration function name.
// Returns NULL for headers that don't need runtime registration.
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
Node *make_strcmp_return_fn_name(Obj *fn, const char *header, const char *fn_name) {
    Node *args[2] = {
        MakeParamRef(fn, "header"),
        MakeStringLiteral(header)
    };
    Node *cmp  = MakeFuncCall(MakeVarRef("strcmp"), args, 2);
    Node *cond = MakeBinary(NK_EQ, cmp, MakeIntLiteral(0));
    return MakeIf(cond, MakeReturn(MakeStringLiteral(fn_name)), NULL);
}

void generate_stdlib_reg_fn(void) {
    char **headers = discover_headers();
    if (!headers) return;

    Type *char_ptr_ty = MakePointer(GetType("char"));
    Obj  *fn          = MakeFunction("get_stdlib_reg_fn_name", char_ptr_ty);
    FunctionAddParam(fn, "header", char_ptr_ty);

    WithFn(fn) {
        // Filter to only headers that have a registration function
        int total = 0;
        for (int i = 0; headers[i]; i++) {
            if (reg_fn_for_header(headers[i])) total++;
        }
        if (!total) {
            FunctionSetBody(fn, MakeReturn(MakeIntLiteral(0)));
            PublishNode(fn);
            return;
        }

        Node *first_char = MakeUnary(NK_DEREF, MakeParamRef(fn, "header"));
        Node *sw = MakeSwitch(first_char);

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

            Node **case_stmts = malloc((bucket_size + 1) * sizeof(Node *));
            int cn = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] != c) continue;
                const char *fn_name = reg_fn_for_header(headers[j]);
                if (!fn_name) continue;
                case_stmts[cn++] = make_strcmp_return_fn_name(fn, headers[j], fn_name);
            }
            case_stmts[cn++] = MakeReturn(MakeIntLiteral(0));

            Node *case_body = MakeBlock(case_stmts, cn);
            SwitchAddCase(sw, MakeIntLiteral(c), case_body);
        }

        Node **stmts = malloc(3 * sizeof(Node *));
        int n = 0;
        stmts[n++] = sw;
        stmts[n++] = MakeReturn(MakeIntLiteral(0));
        FunctionSetBody(fn, MakeBlock(stmts, n));
    }

    PublishNode(fn);
}

void generate_std_header(void) {
    char **headers = discover_headers();
    if (!headers)
        return;

    Type *char_ty     = GetType("char");
    Type *char_ptr_ty = MakePointer(char_ty);
    Obj  *fn          = MakeFunction("get_std_header", char_ptr_ty);
    FunctionAddParam(fn, "filename", char_ptr_ty);

    WithFn(fn) {
        // Count total headers that exist on disk and emit their globals.
        int total = 0;
        for (int i = 0; headers[i]; i++) {
            char *content = read_header_file(header_source_path(headers[i]));
            if (!content) continue;

            int content_len = (int)strlen(content) + 1;
            char *gname   = make_global_name(headers[i]);
            Type *arr_ty = MakeArray(char_ty, content_len);
            Obj  *gvar   = GlobalVar(gname, arr_ty);
            GlobalVarSetInitData(gvar, content, content_len);
            GlobalVarSetStatic(gvar, 1);
            total++;
        }

        // Build a trie dispatching on filename[0] (first char of the header
        // name). Within each case, sequential strcmp checks are used. Since
        // most first-char buckets are small (1-3 headers), this reduces the
        // average number of comparisons from O(N) to O(bucket_size).
        Node *first_char = MakeUnary(NK_DEREF, MakeParamRef(fn, "filename"));
        Node *sw = MakeSwitch(first_char);

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
            Node **case_stmts = malloc((bucket_size + 1) * sizeof(Node *));
            int cn = 0;
            for (int j = 0; headers[j]; j++) {
                if ((unsigned char)headers[j][0] != c) continue;
                if (!read_header_file(header_source_path(headers[j]))) continue;
                case_stmts[cn++] = make_strcmp_return(fn, char_ptr_ty, headers[j]);
            }
            case_stmts[cn++] = MakeReturn(MakeIntLiteral(0));

            Node *case_body = MakeBlock(case_stmts, cn);
            SwitchAddCase(sw, MakeIntLiteral(c), case_body);
        }

        Node **stmts = malloc(3 * sizeof(Node *));
        int n = 0;
        stmts[n++] = sw;
        stmts[n++] = MakeReturn(MakeIntLiteral(0));
        FunctionSetBody(fn, MakeBlock(stmts, n));
    }

    PublishNode(fn);
}

void generate_stdlib_mark_headers(void) {
    char **headers = discover_headers();
    if (!headers) return;

    Type *int_ty       = GetType("int");
    Type *char_ptr_ty  = MakePointer(GetType("char"));
    Obj  *fn           = MakeFunction("get_std_header_name", char_ptr_ty);
    FunctionAddParam(fn, "i", int_ty);

    WithFn(fn) {
        int total = 0;
        for (int n = 0; headers[n]; n++) {
            if (read_header_file(header_source_path(headers[n]))) total++;
        }
        if (!total) {
            FunctionSetBody(fn, MakeReturn(MakeIntLiteral(0)));
            PublishNode(fn);
            return;
        }

        Node *sw = MakeSwitch(MakeParamRef(fn, "i"));

        int idx = 0;
        for (int n = 0; headers[n]; n++) {
            if (!read_header_file(header_source_path(headers[n]))) continue;
            SwitchAddCase(sw, MakeIntLiteral(idx), MakeReturn(MakeStringLiteral(headers[n])));
            idx++;
        }
        SwitchSetDefault(sw, MakeReturn(MakeIntLiteral(0)));

        Node **stmts = malloc(2 * sizeof(Node *));
        int n = 0;
        stmts[n++] = sw;
        FunctionSetBody(fn, MakeBlock(stmts, n));
    }

    PublishNode(fn);
}

generate_std_header();
generate_stdlib_reg_fn();
generate_stdlib_mark_headers();
