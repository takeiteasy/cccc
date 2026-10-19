// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: omp=42
//
// An -I naming CCCC's own include directory reaches the host compiler after
// the host headers, even when the program includes nothing from it: the
// OpenMP shim's <time.h> must be the host's.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        src[512], bin[512], cmd[2048];
    snprintf(src, sizeof(src), "%s/bundled_include_dir.c", out_dir);
    snprintf(bin, sizeof(bin), "%s/bundled_include_dir", out_dir);
    if (WriteFile(ctx, src,
                  "#pragma cccc comptime begin\n"
                  "#pragma cccc emit begin\n"
                  "int sum4(void) {\n"
                  "    int sum = 0;\n"
                  "#pragma omp parallel for reduction(+ : sum)\n"
                  "    for (int i = 0; i < 4; i++)\n"
                  "        sum += i;\n"
                  "    return sum;\n"
                  "}\n"
                  "#pragma cccc emit end\n"
                  "#pragma cccc comptime end\n"
                  "int main(void) { return sum4() == 6 ? 42 : 1; }\n") != 0)
        return 1;
    snprintf(cmd, sizeof(cmd),
             "sh -c '%s -I./include -fopenmp -c=native -o %s %s 2>&1 "
             "&& %s; echo \"omp=$?\"'",
             CcccPath(ctx), bin, src, bin);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
