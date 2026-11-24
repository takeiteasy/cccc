// Every `[[ns::name]]` attribute in docs/, examples/ and include/ is written
// in a namespace its registry row has. A deliberate mismatch carries
// `attr-ns: ok` on its line, or a code block is covered by
// `<!-- attr-ns: ok -->` on the line before its opening fence.

#include "internal.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failed  = 0;
static int checked = 0;

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

static bool is_ident(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

// Checks each item of the attribute list in [p, end).
static void check_list(const char *path, int line, const char *p,
                       const char *end) {
    while (p < end) {
        while (p < end && (*p == ' ' || *p == ','))
            p++;
        const char *ns = NULL, *name = p;
        size_t      ns_len = 0;
        while (p < end && is_ident(*p))
            p++;
        if (p + 1 < end && p[0] == ':' && p[1] == ':') {
            ns     = name;
            ns_len = (size_t)(p - name);
            name = p += 2;
            while (p < end && is_ident(*p))
                p++;
        }
        size_t         len = (size_t)(p - name);
        const AttrDef *def = len ? lookup_attr(name, len) : NULL;
        if (def) {
            checked++;
            if (!attr_in_ns(def, ns, ns_len)) {
                fprintf(stderr,
                        "FAIL: %s:%d: [[%.*s%s%.*s]] is outside its "
                        "namespaces; write [[%s%.*s]] or mark it "
                        "`attr-ns: ok`\n",
                        path, line, (int)ns_len, ns ? ns : "", ns ? "::" : "",
                        (int)len, name, attr_spelling_ns(def), (int)len, name);
                failed = 1;
            }
        }
        // Skip to the next top-level comma.
        for (int depth = 0; p < end && (depth || *p != ','); p++)
            depth += *p == '(' ? 1 : *p == ')' ? -1 : 0;
    }
}

static void check_file(const char *path) {
    char *src = slurp(path);
    if (!src)
        return;
    bool in_fence = false, fence_ok = false, prev_ok = false;
    int  line = 1;
    for (char *l = src; *l; line++) {
        char *nl = strchr(l, '\n');
        if (nl)
            *nl = 0;
        bool ok = strstr(l, "attr-ns: ok") != NULL;
        if (!strncmp(l, "```", 3)) {
            in_fence = !in_fence;
            fence_ok = in_fence && prev_ok;
        } else if (!ok && !(in_fence && fence_ok)) {
            for (char *p = l; (p = strstr(p, "[["));) {
                char *close = strstr(p + 2, "]]");
                if (!close)
                    break;
                check_list(path, line, p + 2, close);
                p = close + 2;
            }
        }
        prev_ok = ok;
        if (!nl)
            break;
        l = nl + 1;
    }
    free(src);
}

static bool has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && !strcmp(s + n - m, suffix);
}

static void walk(const char *dir, const char *const *suffixes) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "FAIL: cannot open %s (run from the repo root)\n", dir);
        failed = 1;
        return;
    }
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            walk(path, suffixes);
            continue;
        }
        for (const char *const *s = suffixes; *s; s++)
            if (has_suffix(path, *s))
                check_file(path);
    }
    closedir(d);
}

int main(void) {
    static const char *const md[]  = {".md", NULL};
    static const char *const src[] = {".c", ".h", NULL};
    walk("docs", md);
    walk("examples", src);
    walk("include", src);
    if (!checked) {
        fprintf(stderr, "FAIL: no registry attributes found\n");
        failed = 1;
    }
    if (failed)
        return 1;
    printf("PASS: doc attribute namespaces (%d checked)\n", checked);
    return 0;
}
