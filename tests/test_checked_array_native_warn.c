// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: warning: -c=native ignores VM runtime safety/debug
// options \(--checked-pointers\): they are enforced by the CCCC VM only.*run=42
//
// #487: same -c=native + --checked-pointers contract as the checked-pointer
// attributes (test_checked_pointers_native_warn.c) -- a checked array's
// enforcement is CHKR, VM-only by design, so the combination warns and still
// compiles and runs cleanly rather than refusing to build.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        src[512], bin[512], cmd[1024];

    snprintf(src, sizeof(src), "%s/checked_array_native_warn.c", out_dir);
    FILE *f = fopen(src, "w");
    if (!f)
        return 1;
    fputs("int main(void) { int a _Checked[10]; a[0] = 1; return a[0] == 1 "
          "? 42 : 1; }\n",
          f);
    fclose(f);

    snprintf(bin, sizeof(bin), "%s/checked_array_native_warn_bin", out_dir);
    snprintf(cmd, sizeof(cmd),
             "sh -c './cccc -c=native --checked-pointers -o %s %s 2>&1; "
             "%s; echo \"run=$?\"'",
             bin, src, bin);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
