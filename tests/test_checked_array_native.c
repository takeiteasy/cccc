// CCCC_FLAGS: --build
// CCCC_REJECT_STDOUT: ignores VM runtime safety/debug options
// CCCC_EXPECT_STDOUT: CHECKED BOUNDS VIOLATION[\s\S]*run=255
//
// -c=native enforces --checked-pointers on a _Checked array: an
// out-of-bounds index prints the VM's banner and exits 255.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        src[512], bin[512], cmd[2048];

    snprintf(src, sizeof(src), "%s/checked_array_native.c", out_dir);
    if (WriteFile(ctx, src,
                  "int main(void) { int a _Checked[10]; a[0] = 1; "
                  "volatile int i = 10; return a[i]; }\n") != 0)
        return 1;

    snprintf(bin, sizeof(bin), "%s/checked_array_native_bin", out_dir);
    snprintf(cmd, sizeof(cmd),
             "sh -c '%s -c=native --checked-pointers -o %s %s 2>&1 "
             "&& %s; echo \"run=$?\"'",
             CcccPath(ctx), bin, src, bin);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
