// Audit that every comptime builtin prototyped in include/cccc/reflection.h is
// registered with CC_BUILTIN / CC_BUILTIN_VARIADIC / CC_BUILTIN_DOUBLE above
// its definition in src/*.c, with the arity, variadic flag and return kind the
// prototype implies, and that nothing is registered twice or without a
// prototype.
//
// Usage:
//     ./cccc tools/audit_reflection_builtins.c --
#include <ctype.h>
#include <glob.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **v;
    size_t n;
} StrList;

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("audit_reflection_builtins: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static char *strip_dup(const char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    char *out = xmalloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void strlist_push(StrList *l, const char *s) {
    l->v         = xrealloc(l->v, (l->n + 1) * sizeof(char *));
    l->v[l->n++] = strip_dup(s);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static char *path_join(const char *a, const char *b) {
    size_t n   = strlen(a) + strlen(b) + 2;
    char  *out = xmalloc(n);
    snprintf(out, n, "%s/%s", a, b);
    return out;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = xmalloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        die("cannot read %s", path);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

// <root>/tools/x.c -> <root>
static char *repo_root(const char *tool_file) {
    char resolved[PATH_MAX];
    if (!realpath(tool_file, resolved))
        die("cannot resolve %s", tool_file);
    for (int i = 0; i < 2; i++) {
        char *slash = strrchr(resolved, '/');
        if (!slash || slash == resolved)
            die("unexpected tool path %s", tool_file);
        *slash = '\0';
    }
    return strip_dup(resolved);
}

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
    while (t[p] && isspace((unsigned char)t[p]))
        p++;
    return p;
}

static char *dup_stripped(const char *t, size_t a, size_t b) {
    char *raw = xmalloc(b - a + 1);
    memcpy(raw, t + a, b - a);
    raw[b - a] = '\0';
    char *out  = strip_dup(raw);
    free(raw);
    return out;
}

// Blank comment bodies while preserving newlines so offsets and line numbers
// still map back to the source. Block comments first, then `//` to end of
// line -- not string-literal aware.
static char *strip_comments(const char *text) {
    size_t len = strlen(text);
    char  *s   = xmalloc(len + 1);
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
            protos.v   = xrealloc(protos.v, (protos.n + 1) * sizeof(Proto));
            Proto *pr  = &protos.v[protos.n++];
            pr->ret    = dup_stripped(t, s, m.name_start);
            pr->name   = name;
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
    char       *tail = strip_dup(last ? last + 1 : params);
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

typedef struct {
    char *name;
    int   nargs, variadic, returns_double;
    char *file;
} Site;

// CC_BUILTIN{,_VARIADIC,_DOUBLE}(name, n); at the start of a line
static void scan_sites(const char *file, const char *text, Site **sites,
                       size_t *nsites) {
    static const struct {
        const char *macro;
        int         variadic, returns_double;
    } KINDS[] = {{"CC_BUILTIN_VARIADIC(", 1, 0},
                 {"CC_BUILTIN_DOUBLE(", 0, 1},
                 {"CC_BUILTIN(", 0, 0}};
    for (const char *line = text; line && *line;) {
        for (size_t k = 0; k < sizeof KINDS / sizeof *KINDS; k++) {
            size_t ml = strlen(KINDS[k].macro);
            if (strncmp(line, KINDS[k].macro, ml) != 0)
                continue;
            const char *p     = line + ml;
            const char *comma = strchr(p, ',');
            const char *end   = strchr(p, ')');
            if (!comma || !end || comma > end)
                die("malformed %s in %s",
                    KINDS[k].macro, file);
            *sites            = xrealloc(*sites, (*nsites + 1) * sizeof(Site));
            Site *s           = &(*sites)[(*nsites)++];
            s->name           = dup_stripped(p, 0, (size_t)(comma - p));
            s->nargs          = atoi(comma + 1);
            s->variadic       = KINDS[k].variadic;
            s->returns_double = KINDS[k].returns_double;
            s->file           = (char *)file;
            break;
        }
        const char *nl = strchr(line, '\n');
        line           = nl ? nl + 1 : NULL;
    }
}

static const Proto *find_proto(const ProtoList *l, const char *name) {
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i].name, name) == 0)
            return &l->v[i];
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1)
        die("unexpected argument '%s'", argv[1]);

    char     *root = repo_root(__FILE__);
    char     *text = read_file(path_join(root, "include/cccc/reflection.h"));

    StrList   unmatched = {0};
    ProtoList protos    = parse_prototypes(text, &unmatched);
    if (unmatched.n) {
        qsort(unmatched.v, unmatched.n, sizeof(char *), cmp_str);
        fputs("audit_reflection_builtins: __builtin_* declaration(s) in "
              "reflection.h the prototype scanner failed to parse -- fix the "
              "scanner:",
              stderr);
        for (size_t i = 0; i < unmatched.n; i++)
            fprintf(stderr, "%s%s", i ? ", " : " ", unmatched.v[i]);
        fputc('\n', stderr);
        return 1;
    }

    Site  *sites  = NULL;
    size_t nsites = 0;
    glob_t files;
    memset(&files, 0, sizeof(files));
    if (glob(path_join(root, "src/*.c"), 0, NULL, &files) != 0)
        die("no source files under src/");
    for (size_t i = 0; i < files.gl_pathc; i++)
        scan_sites(files.gl_pathv[i], read_file(files.gl_pathv[i]), &sites,
                   &nsites);
    globfree(&files);

    int bad = 0;
    for (size_t i = 0; i < nsites; i++) {
        const Site  *s  = &sites[i];
        const Proto *pr = find_proto(&protos, s->name);
        if (!pr) {
            printf(
                "%s: %s is registered but has no prototype in reflection.h\n",
                s->file, s->name);
            bad = 1;
            continue;
        }
        for (size_t j = 0; j < i; j++)
            if (strcmp(sites[j].name, s->name) == 0) {
                printf("%s: %s is registered more than once\n", s->file,
                       s->name);
                bad = 1;
            }
        int variadic;
        int n  = param_list_arity(pr->params, &variadic);
        int rk = return_kind(pr->ret);
        if (n != s->nargs || variadic != s->variadic ||
            rk != s->returns_double) {
            printf("%s: %s registered as (%d args%s, return kind %d) but "
                   "reflection.h implies (%d args%s, return kind %d)\n",
                   s->file, s->name, s->nargs, s->variadic ? ", variadic" : "",
                   s->returns_double, n, variadic ? ", variadic" : "", rk);
            bad = 1;
        }
    }
    for (size_t i = 0; i < protos.n; i++) {
        int found = 0;
        for (size_t j = 0; j < nsites; j++)
            found |= strcmp(sites[j].name, protos.v[i].name) == 0;
        if (!found) {
            printf("%s is declared in reflection.h but has no CC_BUILTIN "
                   "registration in src/\n",
                   protos.v[i].name);
            bad = 1;
        }
    }
    if (bad)
        return 1;
    printf("audit_reflection_builtins: ok (%zu builtins)\n", protos.n);
    return 0;
}
