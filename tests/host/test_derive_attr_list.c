// The names is_derive_builtin_attr hints at (attributes that need
// #include <cccc/derive.h>) match the handlers include/cccc/derive.h
// registers.

#include "internal.h"

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

int main(void) {
    char *src = slurp("include/cccc/derive.h");
    CHECK(src, "cannot read include/cccc/derive.h (run from the repo root)");
    if (!src)
        return 1;

    int registered = 0;
    for (const char *p = src; (p = strstr(p, "attribute(")); p += 10) {
        const char *name = p + 10;
        while (*name == ' ' || *name == '\n')
            name++;
        if (*name != '"')
            continue;
        name++;
        const char *end = strchr(name, '"');
        registered++;
        CHECK(is_derive_builtin_attr(name, (int)(end - name)),
              "derive.h registers '%.*s' but it is not in the list",
              (int)(end - name), name);
    }

    int listed = 0;
    for (const char *const *n = derive_builtin_attr_names(); *n; n++) {
        listed++;
        char needle[128];
        snprintf(needle, sizeof needle, "\"%s\")", *n);
        CHECK(strstr(src, needle), "'%s' is listed but derive.h has no handler",
              *n);
        CHECK(cc_has_attribute(*n, (int)strlen(*n)),
              "'%s' is listed but cc_has_attribute is false", *n);
    }
    CHECK(registered == listed, "derive.h has %d handlers, list has %d",
          registered, listed);

    free(src);
    if (!failed)
        printf("PASS: derive attribute list matches derive.h\n");
    return failed;
}
