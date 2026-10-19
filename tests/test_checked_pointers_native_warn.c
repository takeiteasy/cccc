// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: -c=native ignores VM runtime safety/debug options \(--checked-pointers\)[\s\S]*run=42
//
// -c=native with --checked-pointers warns, drops the flag and still builds
// a program that runs: the checked-pointer attributes are stripped from
// native output and their enforcement is VM-only.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        src[512], bin[512], cmd[2048];

    snprintf(src, sizeof(src), "%s/checked_native_warn.c", out_dir);
    if (WriteFile(ctx, src,
                  "int f(int * [[cccc::array, cccc::count(3)]] p) "
                  "{ return p[0]; }\n"
                  "int main(void) { int x[3] = {1, 2, 3}; return f(x) + 41; }"
                  "\n") != 0)
        return 1;

    snprintf(bin, sizeof(bin), "%s/checked_native_warn_bin", out_dir);
    snprintf(cmd, sizeof(cmd),
             "sh -c '%s -c=native --checked-pointers -o %s %s 2>&1; "
             "%s; echo \"run=$?\"'",
             CcccPath(ctx), bin, src, bin);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
