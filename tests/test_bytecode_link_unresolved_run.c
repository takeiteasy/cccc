// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: unresolved external: undefined_fn.*exit=1
// CCCC_REJECT_STDOUT: STACK OVERFLOW
//
// Negative-path regression for #898: `--link lib.c4a` without `-o`, where
// the linked library does *not* define the called symbol, must fail
// cleanly with "unresolved external" and a non-zero exit -- not fall
// through to running a corrupt image (the STACK OVERFLOW crash from the
// original ticket). This is the check that the unresolved-relocation
// checks moved with the link pass (now run unconditionally right after
// codegen) rather than staying behind in the old -o-only code path.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char lib_out[512], main_src[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/mathlib_link_unresolved_test.c4a", out_dir);
    snprintf(main_src, sizeof(main_src), "%s/link_unresolved_main.c", out_dir);

    // A library that does not define undefined_fn.
    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/lib && ./cccc "
        "examples/build_bytecode_libs_demo/src/math_lib.c "
        "-I examples/build_bytecode_libs_demo/include "
        "--compile=bytecode -o %s 2>&1",
        out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    FILE *f = fopen(main_src, "w");
    if (!f) return 1;
    fputs("void undefined_fn(void);\n"
          "int main(void) { undefined_fn(); return 1; }\n", f);
    fclose(f);

    // No -o: exercise the in-memory run path.
    snprintf(cmd, sizeof(cmd),
        "sh -c './cccc %s --link %s 2>&1; echo \"exit=$?\"'",
        main_src, lib_out);
    const char *result = CaptureCommand(ctx, cmd);
    if (result) puts(result);
    return 0;
}
