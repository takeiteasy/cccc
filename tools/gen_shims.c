// Generate src/shims.inc (the -c=native support-shim text table) from the
// ordinary C in src/shims/*.c.
//
// Each shim body lives as real C under src/shims/, one file per emitter group,
// each chunk delimited by
//
//     // >>> shim: <name>
//     ...C...
//     // <<< shim
//
// and becomes a `static const char CCCC_SHIM_<group>_<name>[]` in
// src/shims.inc, which src/serialize_shims.c #includes and references by name,
// so a typo'd chunk name is a compile error rather than a silently-missing
// shim. Whole-line
// `//` comments and blank lines at the top/bottom of a chunk are dropped; the
// text is otherwise emitted verbatim.
//
// src/shims.inc is committed so plain `make` needs no python3. `./cccc --build
// build.c`'s default build regenerates it via the `shims_gen` step; `--check`
// (wired into the `test` build target) fails if the committed file is stale.
//
// Usage:
//     ./cccc tools/gen_shims.c --         # write/refresh src/shims.inc
//     ./cccc tools/gen_shims.c -- --check # verify it is up to date
#include "lib/args.c"
#include "lib/fs.c"
#include "lib/str.c"

// emitter-group files, in the order sections appear in src/shims.inc
static const char *const GROUPS[] = {
    "native_accessor",
    "reallocarray",
    "threads",
    "uchar",
    "posix_compat",
    "canonical_const",
    "dlfcn",
    "c23_fromfp",
    "wide_bitint",
    "pool",
    "omp",
    "kernel",
};
#define NGROUPS ((int)(sizeof(GROUPS) / sizeof(GROUPS[0])))

// wide_bitint's text doubles as the VM's own compiled src/stdlib/wide_bitint.c,
// so VM and -c=native agree on multi-word _BitInt arithmetic by construction.
static const char *group_source_override(const char *group) {
    if (strcmp(group, "wide_bitint") == 0)
        return "src/stdlib/wide_bitint.c";
    return NULL;
}

static const char BANNER[] =
    "/* GENERATED FILE -- DO NOT EDIT.\n"
    " * Produced by tools/gen_shims.py from the src/shims/ directory.\n"
    " * Regenerate with: python3 tools/gen_shims.py\n"
    " */\n";

typedef struct {
    char *name;
    char *text;
} Section;

typedef struct {
    Section *v;
    size_t   n;
} SectionList;

static const char *skip_ws(const char *p) {
    while (*p && str_is_space((unsigned char)*p))
        p++;
    return p;
}

// ^//\s*>>>\s*shim:\s*(\S+)\s*$
static char *match_open(const char *line) {
    const char *p = line;
    if (strncmp(p, "//", 2) != 0)
        return NULL;
    p = skip_ws(p + 2);
    if (strncmp(p, ">>>", 3) != 0)
        return NULL;
    p = skip_ws(p + 3);
    if (strncmp(p, "shim:", 5) != 0)
        return NULL;
    p                 = skip_ws(p + 5);
    const char *start = p;
    while (*p && !str_is_space((unsigned char)*p))
        p++;
    if (p == start)
        return NULL;
    const char *end = p;
    if (*skip_ws(p) != '\0')
        return NULL;
    char *name = tool_xmalloc((size_t)(end - start) + 1);
    memcpy(name, start, (size_t)(end - start));
    name[end - start] = '\0';
    return name;
}

// ^//\s*<<<\s*shim\s*$
static int match_close(const char *line) {
    const char *p = line;
    if (strncmp(p, "//", 2) != 0)
        return 0;
    p = skip_ws(p + 2);
    if (strncmp(p, "<<<", 3) != 0)
        return 0;
    p = skip_ws(p + 3);
    if (strncmp(p, "shim", 4) != 0)
        return 0;
    return *skip_ws(p + 4) == '\0';
}

static int is_line_comment(const char *ln) {
    return str_starts_with(skip_ws(ln), "//");
}

static void section_push(SectionList *l, char *name, char *text) {
    l->v            = tool_xrealloc(l->v, (l->n + 1) * sizeof(Section));
    l->v[l->n].name = name;
    l->v[l->n].text = text;
    l->n++;
}

static SectionList parse_group(const char *root, const char *group) {
    const char *override = group_source_override(group);
    char        rel[256];
    if (override)
        snprintf(rel, sizeof(rel), "%s", override);
    else
        snprintf(rel, sizeof(rel), "src/shims/%s.c", group);
    char *path  = path_join(root, rel);

    char *text  = fs_read_text_or_die(path, NULL);
    Lines lines = str_split_lines(text);
    free(text);

    SectionList out = {0};
    char       *cur = NULL;
    // body lines of the currently open section (pointers into `lines`)
    const char **bv = tool_xmalloc((lines.n + 1) * sizeof(char *));
    size_t       bn = 0;

    for (size_t i = 0; i < lines.n; i++) {
        const char *raw    = lines.v[i];
        size_t      lineno = i + 1;
        char       *name   = match_open(raw);
        if (name) {
            if (cur)
                tool_die("%s:%zu: nested '>>> shim:' (previous '%s' never "
                         "closed)",
                         path, lineno, cur);
            cur = name;
            bn  = 0;
            continue;
        }
        if (match_close(raw)) {
            if (!cur)
                tool_die("%s:%zu: '<<< shim' with no open", path, lineno);
            // drop whole-line // comments; trim leading/trailing blank lines
            size_t       kn   = 0;
            const char **kept = tool_xmalloc((bn + 1) * sizeof(char *));
            for (size_t k = 0; k < bn; k++)
                if (str_is_blank(bv[k]) || !is_line_comment(bv[k]))
                    kept[kn++] = bv[k];
            size_t lo = 0, hi = kn;
            while (lo < hi && str_is_blank(kept[lo]))
                lo++;
            while (hi > lo && str_is_blank(kept[hi - 1]))
                hi--;
            if (lo == hi)
                tool_die("%s: section '%s' is empty", path, cur);
            StrBuf sb = {0};
            for (size_t k = lo; k < hi; k++) {
                sb_append(&sb, kept[k]);
                sb_append(&sb, "\n");
            }
            free(kept);
            section_push(&out, cur, sb.data);
            cur = NULL;
            continue;
        }
        if (cur)
            bv[bn++] = raw;
    }
    if (cur)
        tool_die("%s: section '%s' never closed", path, cur);
    free(bv);
    lines_free(&lines);
    free(path);
    return out;
}

static void c_escape(StrBuf *sb, const char *s) {
    for (; *s; s++) {
        if (*s == '\\')
            sb_append(sb, "\\\\");
        else if (*s == '"')
            sb_append(sb, "\\\"");
        else
            sb_append_n(sb, s, 1);
    }
}

// one string-literal per source line, matching how the old fprintf() literals
// were physically laid out -- keeps the .inc reviewable.
static void render_constant(StrBuf *out, const char *symbol, const char *text) {
    Lines  l = str_split_lines(text);
    size_t n = l.n;
    if (n && l.v[n - 1][0] == '\0')
        n--;
    sb_appendf(out, "static const char %s[] =\n    ", symbol);
    for (size_t i = 0; i < n; i++) {
        if (i)
            sb_append(out, "\n    ");
        sb_append(out, "\"");
        c_escape(out, l.v[i]);
        sb_append(out, "\\n\"");
    }
    sb_append(out, ";\n");
    lines_free(&l);
}

static const Section *find_section(const SectionList *l, const char *name) {
    // dict(sections): later duplicates win, but duplicates already die earlier
    const Section *hit = NULL;
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i].name, name) == 0)
            hit = &l->v[i];
    return hit;
}

// sorted re.findall(r"_PC_[A-Z_]+") and the count of re.findall(r"case \d+:")
static void norm_pc(const char *t, StrList *pcs, int *cases) {
    for (const char *p = t; *p;) {
        if (strncmp(p, "_PC_", 4) == 0) {
            const char *q = p + 4;
            while ((*q >= 'A' && *q <= 'Z') || *q == '_')
                q++;
            if (q > p + 4) {
                char *tok = tool_xmalloc((size_t)(q - p) + 1);
                memcpy(tok, p, (size_t)(q - p));
                tok[q - p] = '\0';
                strlist_push(pcs, tok);
                free(tok);
                p = q;
                continue;
            }
        }
        p++;
    }
    strlist_sort(pcs);

    *cases = 0;
    for (const char *p = t; *p;) {
        if (strncmp(p, "case ", 5) == 0) {
            const char *q = p + 5;
            while (*q >= '0' && *q <= '9')
                q++;
            if (q > p + 5 && *q == ':') {
                (*cases)++;
                p = q + 1;
                continue;
            }
        }
        p++;
    }
}

// canonical_const's `pathconf` and `fpathconf` sections share a 9-way _PC_*
// switch; fail if they fall out of sync.
static void pathconf_drift_check(const SectionList *canonical) {
    const Section *a = find_section(canonical, "pathconf");
    const Section *b = find_section(canonical, "fpathconf");
    if (!a || !b)
        return;
    StrList pa = {0}, pb = {0};
    int     ca, cb;
    norm_pc(a->text, &pa, &ca);
    norm_pc(b->text, &pb, &cb);
    int same = ca == cb && pa.n == pb.n;
    for (size_t i = 0; same && i < pa.n; i++)
        same = strcmp(pa.v[i], pb.v[i]) == 0;
    if (!same)
        tool_die("gen_shims: canonical_const pathconf/fpathconf _PC_* "
                 "switches have drifted -- they must stay identical (add the "
                 "case to both).");
}

static char *build(const char *root, size_t *nsections, size_t *len) {
    StrBuf      out       = {0};
    StrList     seen      = {0};
    SectionList canonical = {0};
    size_t      total     = 0;

    sb_append(&out, BANNER);
    for (int g = 0; g < NGROUPS; g++) {
        const char *group    = GROUPS[g];
        SectionList sections = parse_group(root, group);
        if (strcmp(group, "canonical_const") == 0)
            canonical = sections;
        sb_appendf(&out, "\n/* ---- src/shims/%s.c ", group);
        for (int d = 0; d < 58 - (int)strlen(group); d++)
            sb_append(&out, "-");
        sb_append(&out, " */\n");
        for (size_t i = 0; i < sections.n; i++) {
            char symbol[512];
            snprintf(symbol, sizeof(symbol), "CCCC_SHIM_%s_%s", group,
                     sections.v[i].name);
            for (size_t s = 0; s < seen.n; s++)
                if (strcmp(seen.v[s], symbol) == 0)
                    tool_die("gen_shims: duplicate section %s", symbol);
            strlist_push(&seen, symbol);
            render_constant(&out, symbol, sections.v[i].text);
        }
        total += sections.n;
    }
    pathconf_drift_check(&canonical);
    *nsections = total;
    *len       = out.len;
    return out.data;
}

int main(int argc, char **argv) {
    int       check   = 0;
    ArgSpec   specs[] = {{"--check", NULL, 0, &check, NULL}};
    ArgResult r       = args_parse(argc, argv, specs, 1, "gen_shims [--check]");
    if (r.npos)
        tool_die("unexpected argument '%s'", r.pos[0]);

    char  *root    = tool_repo_root(__FILE__);
    char  *out_inc = path_join(root, "src/shims.inc");
    size_t n, len;
    char  *content = build(root, &n, &len);

    if (check) {
        if (!fs_file_equals(out_inc, content, len)) {
            printf("gen_shims: src/shims.inc is stale or missing.\n");
            printf("Run: ./cccc tools/gen_shims.c --\n");
            return 1;
        }
        printf("gen_shims: up to date (%zu shim sections)\n", n);
        return 0;
    }
    if (!fs_write_if_changed(out_inc, content, len)) {
        printf("gen_shims: already up to date (%zu shim sections)\n", n);
        return 0;
    }
    printf("gen_shims: wrote src/shims.inc (%zu shim sections)\n", n);
    return 0;
}
