#include "args.h"
#include <stdlib.h>
#include <string.h>

static void usage_exit(const char *usage, int code, const char *why,
                       const char *arg) {
    FILE *out = code ? stderr : stdout;
    if (why)
        fprintf(out, "%s: %s\n", why, arg);
    fprintf(out, "usage: %s\n", usage);
    exit(code);
}

ArgResult args_parse(int argc, char **argv, const ArgSpec *specs, int nspecs,
                     const char *usage) {
    ArgResult r;
    r.pos  = tool_xmalloc((size_t)(argc + 1) * sizeof(char *));
    r.npos = 0;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
            usage_exit(usage, 0, NULL, NULL);
        if (a[0] != '-' || a[1] == '\0') {
            r.pos[r.npos++] = a;
            continue;
        }
        const ArgSpec *hit        = NULL;
        const char    *inline_val = NULL;
        for (int s = 0; s < nspecs && !hit; s++) {
            size_t nl = strlen(specs[s].name);
            if (strncmp(a, specs[s].name, nl) == 0 &&
                (a[nl] == '\0' || (specs[s].takes_value && a[nl] == '='))) {
                hit = &specs[s];
                if (a[nl] == '=')
                    inline_val = a + nl + 1;
            } else if (specs[s].alias) {
                size_t al = strlen(specs[s].alias);
                if (strncmp(a, specs[s].alias, al) == 0 &&
                    (a[al] == '\0' || specs[s].takes_value)) {
                    hit = &specs[s];
                    if (a[al] != '\0')
                        inline_val = a + al;
                }
            }
        }
        if (!hit)
            usage_exit(usage, 1, "unknown option", a);
        if (!hit->takes_value) {
            *hit->flag = 1;
            continue;
        }
        if (!inline_val) {
            if (i + 1 >= argc)
                usage_exit(usage, 1, "missing value for", a);
            inline_val = argv[++i];
        }
        *hit->value = inline_val;
    }
    return r;
}
