// Real macOS SDK headers (-i $SDK/usr/include) alongside cccc's bundled ones:
// va_list is one type in either include order, and the SDK-only spellings and
// declarations cccc's own sources use resolve. macOS only; skipped without a
// cccc binary ($CCCC_BIN, else build/cccc, else ./cccc) or an SDK.

#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__

static const char VA_STDARG_FIRST[] =
    "#include <stdarg.h>\n#include <stdio.h>\n"
    "static int f(const char *fmt, ...) { va_list a; va_start(a, fmt);"
    " int v = va_arg(a, int); va_end(a); return v; }\n"
    "int main(void) { return f(\"x\", 42); }\n";

static const char VA_STDIO_FIRST[] =
    "#include <stdio.h>\n#include <stdarg.h>\n"
    "static int f(char *b, size_t n, const char *fmt, ...) { va_list a;"
    " va_start(a, fmt); int r = vsnprintf(b, n, fmt, a); va_end(a); return r; }\n"
    "int main(void) { char b[16]; return f(b, sizeof b, \"%d\", 42) == 2 &&"
    " b[0] == '4' ? 42 : 1; }\n";

static const char MISC[] =
    "#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n"
    "#include <string.h>\n#include <unistd.h>\n"
    "int main(void) {\n"
    "    __signed char c = -1;\n"
    "    struct __double2 sc = __sincos_stret(0.0);\n"
    "    char path[] = \"/tmp/cccc_sdk_XXXXXX.txt\";\n"
    "    int fd = mkstemps(path, 4);\n"
    "    if (fd < 0 || c != -1 || sc.__cosval != 1.0)\n"
    "        return 1;\n"
    "    FILE *f = fdopen(fd, \"w\");\n"
    "    if (!f)\n"
    "        return 2;\n"
    "    fputs(\"hi\", f);\n"
    "    fclose(f);\n"
    "    unlink(path);\n"
    "    return 42;\n"
    "}\n";

// Several translation units merged into one -c=native program, built with
// --use-system-headers so the SDK's own spellings reach the host compile.
typedef struct {
    const char *name;
    const char *text;
} SrcFile;

static const char INT64_HEADER[] =
    "#include <stdint.h>\n#include <stdio.h>\n"
    "struct Pair { int64_t a; uint64_t b; };\n"
    "struct Pair pair_make(int64_t a, uint64_t b);\n";

static const SrcFile INT64_PAIR[] = {
    {"pair.h", INT64_HEADER},
    {"pair_other.c", "#include \"pair.h\"\n"
                     "struct Pair pair_make(int64_t a, uint64_t b)"
                     " { return (struct Pair){a, b}; }\n"},
    {"pair_main.c", "#include <stdio.h>\n#include \"pair.h\"\n"
                    "int main(void) { struct Pair p = pair_make(40, 2);"
                    " return (int)(p.a + (int64_t)p.b); }\n"},
    {NULL, NULL},
};

static const SrcFile TOKEN_SHADOW[] = {
    {"audit_user.c",
     "#include <bsm/audit.h>\n"
     "int audit_user(void) { token_t *t = 0; return t == 0; }\n"},
    {"token_main.c",
     "typedef struct token_t token_t;\n"
     "struct token_t { int kind; };\n"
     "int audit_user(void);\n"
     "int main(void) { token_t t = {41}; return t.kind + audit_user(); }\n"},
    {NULL, NULL},
};

static const SrcFile ISNAN_REPLAY[] = {
    {"isnan_main.c",
     "#include <math.h>\n"
     "int main(void) { volatile double d = 0.0;"
     " double n = d / d; return isnan(n) && !isnan(d) ? 42 : 1; }\n"},
    {NULL, NULL},
};

static int run(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static int run(const char *fmt, ...) {
    char    cmd[4 * PATH_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    int st = system(cmd);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int remove_entry(const char *path, const struct stat *st, int type,
                        struct FTW *ftw) {
    (void)st;
    (void)type;
    (void)ftw;
    return remove(path);
}

static int find_sdk(char *sdk, size_t n) {
    FILE *p = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (!p)
        return 0;
    int ok = fgets(sdk, (int)n, p) != NULL;
    pclose(p);
    sdk[strcspn(sdk, "\n")] = '\0';
    return ok && sdk[0] && access(sdk, R_OK) == 0;
}

static int find_cccc(char *out) {
    const char *candidates[] = {getenv("CCCC_BIN"), "build/cccc", "./cccc"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (candidates[i] && realpath(candidates[i], out) &&
            access(out, X_OK) == 0)
            return 1;
    return 0;
}

static int write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f)
        return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

// Both modes must exit 42: the VM run and the -c=native build + run.
static int check(const char *cccc, const char *sdk, const char *root,
                 const char *name, const char *src_text) {
    char src[PATH_MAX], exe[PATH_MAX];
    snprintf(src, sizeof src, "%s/%s.c", root, name);
    snprintf(exe, sizeof exe, "%s/%s", root, name);
    if (!write_file(src, src_text))
        return 1;
    int rc = run("'%s' -i '%s/usr/include' '%s' >/dev/null 2>&1", cccc, sdk,
                 src);
    if (rc != 42) {
        fprintf(stderr, "FAIL: %s under the VM exited %d, want 42\n", name, rc);
        return 1;
    }
    rc = run("'%s' -i '%s/usr/include' -c=native '%s' -o '%s' 2>&1", cccc, sdk,
             src, exe);
    if (rc != 0) {
        fprintf(stderr, "FAIL: %s -c=native build exited %d\n", name, rc);
        return 1;
    }
    rc = run("'%s'", exe);
    if (rc != 42) {
        fprintf(stderr, "FAIL: %s native binary exited %d, want 42\n", name,
                rc);
        return 1;
    }
    return 0;
}

// Exits 42 natively; `cccc_flags` precede the SDK path and sources.
static int check_files(const char *cccc, const char *sdk, const char *root,
                       const char *name, const char *cccc_flags,
                       const SrcFile *files) {
    char srcs[4 * PATH_MAX] = "", exe[PATH_MAX];
    snprintf(exe, sizeof exe, "%s/%s", root, name);
    for (const SrcFile *f = files; f->name; f++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", root, f->name);
        if (!write_file(path, f->text))
            return 1;
        size_t len = strlen(srcs);
        if (strstr(f->name, ".c"))
            snprintf(srcs + len, sizeof srcs - len, " '%s'", path);
    }
    int rc =
        run("'%s' %s -I '%s' -i '%s/usr/include' -c=native %s -o '%s' 2>&1",
            cccc, cccc_flags, root, sdk, srcs, exe);
    if (rc != 0) {
        fprintf(stderr, "FAIL: %s -c=native build exited %d\n", name, rc);
        return 1;
    }
    rc = run("'%s'", exe);
    if (rc != 42) {
        fprintf(stderr, "FAIL: %s native binary exited %d, want 42\n", name,
                rc);
        return 1;
    }
    return 0;
}

int main(void) {
    alarm(240);
    char cccc[PATH_MAX], sdk[PATH_MAX];
    if (!find_cccc(cccc) || !find_sdk(sdk, sizeof sdk)) {
        printf("SKIP: sdk headers (needs a cccc binary and a macOS SDK)\n");
        return 0;
    }
    char root[] = "/tmp/cccc_sdk_headers_XXXXXX";
    if (!mkdtemp(root))
        return 2;

    int failed = check(cccc, sdk, root, "va_stdarg_first", VA_STDARG_FIRST);
    failed    |= check(cccc, sdk, root, "va_stdio_first", VA_STDIO_FIRST);
    failed    |= check(cccc, sdk, root, "misc", MISC);
    failed |= check_files(cccc, sdk, root, "int64_pair", "--use-system-headers",
                          INT64_PAIR);
    failed |= check_files(cccc, sdk, root, "token_shadow",
                          "--use-system-headers", TOKEN_SHADOW);
    failed |= check_files(cccc, sdk, root, "isnan_replay",
                          "--use-system-headers -I include", ISNAN_REPLAY);
    nftw(root, remove_entry, 8, FTW_DEPTH | FTW_PHYS);
    if (!failed)
        printf("PASS: sdk headers\n");
    return failed;
}

#else

int main(void) {
    printf("SKIP: sdk headers (macOS only)\n");
    return 0;
}

#endif
