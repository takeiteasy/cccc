// The builtin registry (src/builtin_registry.c) matches the names the parser
// accepts: every builtin spelled in the parser is answered by __has_builtin,
// and every registry row is spelled in the parser.

#include "internal.h"

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc(n + 1);
    if (buf && fread(buf, 1, n, f) == (size_t)n)
        buf[n] = 0;
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static const char *const prefixes[] = {"__builtin_", "__atomic_", "__sync_",
                                       "__c11_atomic_"};

// Spelled in the parser but not callable builtins.
static const char *const not_builtins[] = {"__builtin_va_list"};

typedef struct {
    char **names;
    int    count;
} NameSet;

static bool set_has(const NameSet *s, const char *name) {
    for (int i = 0; i < s->count; i++)
        if (!strcmp(s->names[i], name))
            return true;
    return false;
}

static void scrape(const char *src, NameSet *out) {
    for (const char *p = src; (p = strchr(p, '"')); p++) {
        const char *name = p + 1;
        bool        hit  = false;
        for (size_t i = 0; i < sizeof prefixes / sizeof *prefixes; i++)
            hit |= !strncmp(name, prefixes[i], strlen(prefixes[i]));
        if (!hit)
            continue;
        const char *end = name;
        while ((*end >= 'a' && *end <= 'z') || (*end >= 'A' && *end <= 'Z') ||
               (*end >= '0' && *end <= '9') || *end == '_')
            end++;
        // A literal that is only a prefix or continues (`"__builtin_%s"`) is
        // not a name.
        if (*end != '"' || end[-1] == '_') {
            p = end - 1;
            continue;
        }
        char *copy = strndup(name, end - name);
        if (!set_has(out, copy)) {
            out->names = realloc(out->names, (out->count + 1) * sizeof(char *));
            out->names[out->count++] = copy;
        } else
            free(copy);
        p = end;
    }
}

int main(void) {
    glob_t g;
    CHECK(!glob("src/parse*.c", 0, NULL, &g) && g.gl_pathc,
          "no src/parse*.c (run from the repo root)");
    if (failed)
        return 1;

    NameSet spelled    = {0};
    char    overload[] = "src/overload.c";
    for (size_t i = 0; i <= g.gl_pathc; i++) {
        const char *path = i < g.gl_pathc ? g.gl_pathv[i] : overload;
        char       *src  = slurp(path);
        CHECK(src, "cannot read %s", path);
        if (src)
            scrape(src, &spelled);
        free(src);
    }
    globfree(&g);

    for (int i = 0; i < spelled.count; i++) {
        const char *name = spelled.names[i];
        bool        skip = false;
        for (size_t j = 0; j < sizeof not_builtins / sizeof *not_builtins; j++)
            skip |= !strcmp(name, not_builtins[j]);
        if (skip)
            continue;
        CHECK(cc_has_builtin(name, strlen(name)),
              "the parser spells '%s' but src/builtin_registry.c has no row",
              name);
    }

    size_t            n;
    const BuiltinDef *table = builtin_table(&n);
    for (size_t i = 0; i < n; i++) {
        CHECK(i == 0 || strcmp(table[i - 1].name, table[i].name) < 0,
              "'%s' is out of order or repeated", table[i].name);
        CHECK(set_has(&spelled, table[i].name),
              "'%s' has a row but the parser does not spell it", table[i].name);
    }

    if (!failed)
        printf("PASS: builtin registry matches the parser (%d spelled, %zu "
               "rows)\n",
               spelled.count, n);
    return failed;
}
