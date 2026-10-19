// CCCC_FLAGS: --build
// CCCC_REJECT_STDOUT: ignores VM runtime safety/debug options
// CCCC_EXPECT_STDOUT: ok=42
// CCCC_EXPECT_STDOUT: CHECKED BOUNDS VIOLATION[\s\S]*discarded=255
// CCCC_EXPECT_STDOUT: Non-null store into a \[\[cccc::ntarray\]\] terminator slot[\s\S]*terminator=255
// CCCC_EXPECT_STDOUT: CHECKED ASSIGNMENT BOUNDS VIOLATION[\s\S]*implication=255
// CCCC_EXPECT_STDOUT: dynamic_check\(\) assertion failed at line 1[\s\S]*dynamic=255
//
// -c=native enforces --checked-pointers: each violation prints the VM's
// banner and exits 255.

#include <stdio.h>

static const char *run(Builder *ctx, const char *name, const char *code) {
    const char *out_dir = BuildOutDir(ctx);
    char        src[512], bin[512], cmd[2048];
    snprintf(src, sizeof(src), "%s/checked_native_%s.c", out_dir, name);
    snprintf(bin, sizeof(bin), "%s/checked_native_%s", out_dir, name);
    if (WriteFile(ctx, src, code) != 0)
        return NULL;
    snprintf(cmd, sizeof(cmd),
             "sh -c '%s -c=native --checked-pointers -o %s %s 2>&1 "
             "&& %s; echo \"%s=$?\"'",
             CcccPath(ctx), bin, src, bin, name);
    return CaptureCommand(ctx, cmd);
}

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *programs[][2] = {
        {"ok", "int main(void) {\n"
               "    int x[3] = {1, 2, 3};\n"
               "    volatile int *[[cccc::array, cccc::count(3)]] vp = x;\n"
               "    int (*[[cccc::array, cccc::count(1)]] pa)[3] = &x;\n"
               "    vp[1] = 40;\n"
               "    return (*pa)[1] + vp[2] - 1;\n"
               "}\n"},
        {"discarded", "int main(void) {\n"
                      "    int x[3] = {1, 2, 3};\n"
                      "    int *[[cccc::array, cccc::count(3)]] p = x;\n"
                      "    volatile int i = 3;\n"
                      "    (void)p[i];\n"
                      "    return 42;\n"
                      "}\n"},
        {"terminator", "int main(void) {\n"
                       "    int n = 3;\n"
                       "    char *[[cccc::ntarray, cccc::count(n)]] s =\n"
                       "        (char[4]){'a', 'b', 'c', 0};\n"
                       "    volatile int i = 3;\n"
                       "    s[i] = 'x';\n"
                       "    return 42;\n"
                       "}\n"},
        {"implication",
         "void sink(int *[[cccc::array, cccc::count(n)]] p, int n) {\n"
         "    (void)p;\n"
         "    (void)n;\n"
         "}\n"
         "int main(void) {\n"
         "    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};\n"
         "    sink(small, 8);\n"
         "    return 42;\n"
         "}\n"},
        {"dynamic", "int main(void) { volatile int i = 10, n = 5; "
                    "__builtin_cccc_dynamic_check(i < n); return 42; }\n"},
    };
    for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++) {
        const char *result = run(ctx, programs[i][0], programs[i][1]);
        if (result)
            puts(result);
    }
    return 0;
}
