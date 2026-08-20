// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: warning: -c=native ignores VM runtime safety/debug
// options \(--checked-pointers\): they are enforced by the CCCC VM only.*run=42
//
// #924: -c=native --checked-pointers used to be a hard compile-time error
// ("-c=native cannot be combined with VM runtime safety/debug options").
// Pins the new behavior: since checked-pointer attributes are always
// stripped from native output regardless of the flag (#482/#488 ABI
// transparency) and CHKR enforcement is VM-only by design, the combination
// now warns and still compiles and runs cleanly rather than refusing to
// build.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        src[512], bin[512], cmd[1024];

    snprintf(src, sizeof(src), "%s/checked_native_warn.c", out_dir);
    FILE *f = fopen(src, "w");
    if (!f)
        return 1;
    fputs("int f(int * [[cccc::array, cccc::count(3)]] p) { return p[0]; }\n"
          "int main(void) { int x[3] = {1, 2, 3}; return f(x) + 41; }\n",
          f);
    fclose(f);

    snprintf(bin, sizeof(bin), "%s/checked_native_warn_bin", out_dir);
    snprintf(cmd, sizeof(cmd),
             "sh -c './cccc -c=native --checked-pointers -o %s %s 2>&1; "
             "%s; echo \"run=$?\"'",
             bin, src, bin);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
