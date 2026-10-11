// Generate src/reflection_ffi_protos.inc and src/reflection_ffi_register.inc,
// the comptime-builtin FFI block of src/macros.c / src/reflection.c, from
// include/cccc/reflection.h.
//
// reflection.h is the canonical signature for every comptime builtin. This
// tool parses every file-scope `RET __builtin_NAME(params);` prototype out of
// it (in header order) and emits two generated, committed files:
//
//   src/reflection_ffi_protos.inc    one `extern` per builtin, copied verbatim
//                                    from the header's return type / params /
//                                    attribute.
//   src/reflection_ffi_register.inc  the register_reflection_ffi() body: one
//                                    cc_register_cfunc() /
//                                    cc_register_variadic_cfunc() call per
//                                    builtin, with a generated arity.
//
// Both are #include'd by src/macros.c and src/reflection.c, so drift between
// reflection.h's prototype and reflection.c's definition is a compile error,
// not just an unregistered or mismatched-arity builtin.
//
// The .inc files are committed so plain `make` needs no python3. `./cccc
// --build build.c`'s default build regenerates them via the
// `reflection_ffi_gen` step; `--check` (wired into the `test` build target)
// fails if the committed files are stale.
//
// Usage:
//     ./cccc tools/gen_reflection_ffi.c --         # write/refresh the .inc
//     files
//     ./cccc tools/gen_reflection_ffi.c -- --check # verify they're up to date
#include "lib/args.c"
#include "lib/fs.c"
#include "lib/str.c"
#include <ctype.h>

static const char BANNER[] =
    "/* GENERATED FILE -- DO NOT EDIT.\n"
    " * Produced by tools/gen_reflection_ffi.py from "
    "include/cccc/reflection.h.\n"
    " * Regenerate with: python3 tools/gen_reflection_ffi.py\n"
    " */\n";

typedef struct {
    char *ret, *name, *params, *attr;
} Proto;

typedef struct {
    Proto *v;
    size_t n;
} ProtoList;

static int is_word(int c) {
    return isalnum(c) || c == '_';
}

// characters group 1 of a prototype's return type may contain: [\w \t*]
static int in_ret_class(int c) {
    return is_word(c) || c == ' ' || c == '\t' || c == '*';
}

static size_t skip_ws_at(const char *t, size_t p) {
    while (t[p] && str_is_space((unsigned char)t[p]))
        p++;
    return p;
}

static char *dup_stripped(const char *t, size_t a, size_t b) {
    char *raw = tool_xmalloc(b - a + 1);
    memcpy(raw, t + a, b - a);
    raw[b - a] = '\0';
    char *out  = str_strip_dup(raw);
    free(raw);
    return out;
}

// Blank comment bodies while preserving newlines so offsets and line numbers
// still map back to the source. Block comments first, then `//` to end of
// line -- not string-literal aware.
static char *strip_comments(const char *text) {
    size_t len = strlen(text);
    char  *s   = tool_xmalloc(len + 1);
    memcpy(s, text, len + 1);
    for (size_t i = 0; i + 1 < len;) {
        if (s[i] == '/' && s[i + 1] == '*') {
            const char *close = strstr(s + i + 2, "*/");
            if (!close)
                break;
            size_t end = (size_t)(close - s) + 2;
            for (size_t k = i; k < end; k++)
                if (s[k] != '\n')
                    s[k] = ' ';
            i = end;
        } else {
            i++;
        }
    }
    for (size_t i = 0; i + 1 < len;) {
        if (s[i] == '/' && s[i + 1] == '/') {
            while (i < len && s[i] != '\n')
                s[i++] = ' ';
        } else {
            i++;
        }
    }
    return s;
}

typedef struct {
    size_t name_start, name_end; // the return type ends where the name starts
    size_t params_start, params_end;
    size_t attr_start, attr_end; // equal when there is no attribute
    char   term;
    size_t end;                  // one past the terminator
} Match;

// Match of
//   ([A-Za-z_][\w \t*]*?)\b(__builtin_\w+)\s*\((PARAMS)\)\s*(ATTR?)\s*TERM
// anchored at s, where PARAMS = (?:[^;{)(]|\([^()]*\))* and ATTR =
// __attribute__\s*\(<anything up to the terminator>\). `col0` selects the
// completeness-guard flavour: ATTR may not cross '{' either and TERM is ';' or
// '{' rather than just ';'. Group 1 is lazy, so the shortest return type that
// lets the rest match wins.
static int try_match(const char *t, size_t s, int col0, Match *m) {
    if (!(isalpha((unsigned char)t[s]) || t[s] == '_'))
        return 0;
    for (size_t e = s + 1;; e++) {
        if (e - 1 > s && !in_ret_class((unsigned char)t[e - 1]))
            return 0;
        if (is_word((unsigned char)t[e - 1]) ||
            strncmp(t + e, "__builtin_", 10) != 0 ||
            !is_word((unsigned char)t[e + 10]))
            continue;

        size_t k = e + 10;
        while (is_word((unsigned char)t[k]))
            k++;
        size_t name_end = k;
        size_t p        = skip_ws_at(t, k);
        if (t[p] != '(')
            continue;
        p++;
        size_t params_start = p;
        int    ok           = 1;
        for (;;) {
            char c = t[p];
            if (c == '\0' || c == ';' || c == '{') {
                ok = 0;
                break;
            }
            if (c == ')')
                break;
            if (c == '(') {
                size_t q = p + 1;
                while (t[q] && t[q] != '(' && t[q] != ')')
                    q++;
                if (t[q] != ')') {
                    ok = 0;
                    break;
                }
                p = q + 1;
                continue;
            }
            p++;
        }
        if (!ok)
            continue;
        size_t params_end = p;
        p                 = skip_ws_at(t, p + 1);

        size_t attr_start = p, attr_end = p;
        if (strncmp(t + p, "__attribute__", 13) == 0) {
            size_t a = skip_ws_at(t, p + 13);
            if (t[a] != '(')
                continue;
            size_t q = a + 1;
            while (t[q] && t[q] != ';' && !(col0 && t[q] == '{'))
                q++;
            if (!t[q])
                continue;
            size_t r = q;
            while (r > a + 1 && t[r - 1] != ')')
                r--;
            if (r <= a + 1)
                continue;
            size_t close = r - 1; // last ')' before the terminator
            if (skip_ws_at(t, close + 1) != q)
                continue;
            attr_end = close + 1;
            p        = q;
        } else if (!(t[p] == ';' || (col0 && t[p] == '{'))) {
            continue;
        }

        m->name_start   = e;
        m->name_end     = name_end;
        m->params_start = params_start;
        m->params_end   = params_end;
        m->attr_start   = attr_start;
        m->attr_end     = attr_end;
        m->term         = t[p];
        m->end          = p + 1;
        return 1;
    }
}

static int has_name(const ProtoList *l, const char *name) {
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i].name, name) == 0)
            return 1;
    return 0;
}

// Prototypes in header order, plus the names of column-0 builtin signatures
// the scanner failed to capture (the completeness guard).
static ProtoList parse_prototypes(const char *text, StrList *unmatched) {
    char     *t      = strip_comments(text);
    size_t    len    = strlen(t);
    ProtoList protos = {0};

    for (size_t s = 0; s < len;) {
        Match m;
        if (!try_match(t, s, 0, &m)) {
            s++;
            continue;
        }
        char *name = dup_stripped(t, m.name_start, m.name_end);
        if (!has_name(&protos, name)) {
            protos.v  = tool_xrealloc(protos.v, (protos.n + 1) * sizeof(Proto));
            Proto *pr = &protos.v[protos.n++];
            pr->ret   = dup_stripped(t, s, m.name_start);
            pr->name  = name;
            pr->params = dup_stripped(t, m.params_start, m.params_end);
            pr->attr   = dup_stripped(t, m.attr_start, m.attr_end);
        }
        s = m.end;
    }

    // Anything else shaped like a column-0 declaration means the scanner
    // missed a real prototype. A '{' terminator is a function defined with a
    // body in the header (compiled into every macro program, not
    // FFI-registered) and is deliberately treated as handled.
    for (size_t s = 0; s < len;) {
        if (s != 0 && t[s - 1] != '\n') {
            const char *nl = strchr(t + s, '\n');
            if (!nl)
                break;
            s = (size_t)(nl - t) + 1;
            continue;
        }
        Match m;
        if (!try_match(t, s, 1, &m)) {
            s++;
            continue;
        }
        char *name = dup_stripped(t, m.name_start, m.name_end);
        if (m.term != '{' && !has_name(&protos, name)) {
            int dup = 0;
            for (size_t i = 0; i < unmatched->n; i++)
                dup |= strcmp(unmatched->v[i], name) == 0;
            if (!dup)
                strlist_push(unmatched, name);
        }
        free(name);
        // `^` only matches at a line start, so resume at the next one
        const char *nl = strchr(t + m.end, '\n');
        if (!nl)
            break;
        s = (size_t)(nl - t) + 1;
    }
    free(t);
    return protos;
}

// (fixed_arg_count, is_variadic) for a stripped parameter-list string; the
// count is a plain comma split, so a function-pointer parameter's inner
// commas are counted too.
static int param_list_arity(const char *params, int *variadic) {
    *variadic = 0;
    if (params[0] == '\0' || strcmp(params, "void") == 0)
        return 0;
    int n = 1;
    for (const char *p = params; *p; p++)
        if (*p == ',')
            n++;
    const char *last = strrchr(params, ',');
    char       *tail = str_strip_dup(last ? last + 1 : params);
    if (strcmp(tail, "...") == 0) {
        *variadic = 1;
        n--;
    }
    free(tail);
    return n;
}

// 0 = int/pointer-sized, 1 = double, 2 = float (src/vm.c set_return_kind)
static int return_kind(const char *ret) {
    if (strcmp(ret, "double") == 0)
        return 1;
    if (strcmp(ret, "float") == 0)
        return 2;
    return 0;
}

static char *render_protos_inc(const ProtoList *protos, size_t *len) {
    StrBuf sb = {0};
    sb_append(&sb, BANNER);
    sb_append(&sb, "\n\n");
    for (size_t i = 0; i < protos->n; i++) {
        const Proto *p = &protos->v[i];
        sb_appendf(&sb, "extern %s %s(%s)", p->ret, p->name,
                   p->params[0] ? p->params : "void");
        if (p->attr[0])
            sb_appendf(&sb, "\n    %s", p->attr);
        sb_append(&sb, ";\n");
    }
    *len = sb.len;
    return sb.data;
}

static char *render_register_inc(const ProtoList *protos, size_t *len) {
    StrBuf sb = {0};
    sb_append(&sb, BANNER);
    sb_append(&sb, "\n\n");
    for (size_t i = 0; i < protos->n; i++) {
        const Proto *p = &protos->v[i];
        int          variadic;
        int          n = param_list_arity(p->params, &variadic);
        sb_appendf(&sb, "%s(vm, \"%s\", (void *)%s, %d, %d);\n",
                   variadic ? "cc_register_variadic_cfunc"
                            : "cc_register_cfunc",
                   p->name, p->name, n, return_kind(p->ret));
    }
    *len = sb.len;
    return sb.data;
}

int main(int argc, char **argv) {
    int       check   = 0;
    ArgSpec   specs[] = {{"--check", NULL, 0, &check, NULL}};
    ArgResult r =
        args_parse(argc, argv, specs, 1, "gen_reflection_ffi [--check]");
    if (r.npos)
        tool_die("unexpected argument '%s'", r.pos[0]);

    char *root = tool_repo_root(__FILE__);
    char *text =
        fs_read_text_or_die(path_join(root, "include/cccc/reflection.h"), NULL);

    StrList   unmatched = {0};
    ProtoList protos    = parse_prototypes(text, &unmatched);
    if (unmatched.n) {
        strlist_sort(&unmatched);
        StrBuf names = {0};
        for (size_t i = 0; i < unmatched.n; i++)
            sb_appendf(&names, "%s%s", i ? ", " : "", unmatched.v[i]);
        tool_die("gen_reflection_ffi: found __builtin_* declaration(s) in "
                 "reflection.h that the prototype scanner failed to parse -- "
                 "fix the scanner, don't silently drop a builtin from the FFI "
                 "table: %s",
                 names.data);
    }

    static const char *const REL[2] = {"src/reflection_ffi_protos.inc",
                                       "src/reflection_ffi_register.inc"};
    char                    *content[2];
    size_t                   clen[2];
    content[0] = render_protos_inc(&protos, &clen[0]);
    content[1] = render_register_inc(&protos, &clen[1]);

    if (check) {
        int stale = 0;
        for (int i = 0; i < 2; i++) {
            char *path = path_join(root, REL[i]);
            if (!fs_file_equals(path, content[i], clen[i])) {
                if (!stale)
                    printf("gen_reflection_ffi: stale/missing generated "
                           "file(s):\n");
                printf("  %s\n", REL[i]);
                stale = 1;
            }
        }
        if (stale) {
            printf("Run: ./cccc tools/gen_reflection_ffi.c --\n");
            return 1;
        }
        printf("gen_reflection_ffi: up to date (%zu builtins)\n", protos.n);
        return 0;
    }

    int changed[2], nchanged = 0;
    for (int i = 0; i < 2; i++) {
        char *path  = path_join(root, REL[i]);
        changed[i]  = fs_write_if_changed(path, content[i], clen[i]);
        nchanged   += changed[i];
    }
    if (nchanged) {
        printf("gen_reflection_ffi: regenerated %d file(s) (%zu builtins):\n",
               nchanged, protos.n);
        for (int i = 0; i < 2; i++)
            if (changed[i])
                printf("  %s\n", REL[i]);
    } else {
        printf("gen_reflection_ffi: already up to date (%zu builtins)\n",
               protos.n);
    }
    return 0;
}
