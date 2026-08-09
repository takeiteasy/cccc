// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: ngrams:.*--link is not supported when running a prebuilt \.c4 file.*exit=1.*fusion:.*--link is not supported when running a prebuilt \.c4 file.*exit=1
//
// Regression for #902: the "all inputs are .c4" static-analysis dispatch
// (--ngrams/--fusion-candidates walking one or more prebuilt .c4 files via
// cc_load_bytecode()) accepted --link on the CLI but silently ignored it --
// same class of silent no-op that #898 fixed for --link without -o. The
// sibling single-.c4 run/--testing/--disassemble path already rejects
// --link against a prebuilt .c4 with a clean error ("--link is not
// supported when running a prebuilt .c4 file"); this pins that same error
// down for the analysis dispatch, for both --ngrams and --fusion-candidates.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char lib_out[512], app_out[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/mathlib_link_ngrams_test.c4a", out_dir);
    snprintf(app_out, sizeof(app_out), "%s/bin/app_link_ngrams_test.c4", out_dir);

    // Build a library.
    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/lib && ./cccc "
        "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include "
        "--compile=bytecode -o %s 2>&1",
        out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    // Build a real prebuilt, already-linked .c4 to run --ngrams/--fusion
    // against (the analysis path is what's under test, not linking itself).
    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/bin && ./cccc "
        "tests/fixtures/build_bytecode_libs_demo/src/main.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include --link %s -o %s 2>&1",
        out_dir, lib_out, app_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    // --ngrams over a prebuilt .c4 with --link still on the command line:
    // must error, not silently ignore --link. -0 -V pins the VM safety
    // flags to CCCC_VM_HEAP-only (still cleared by -V) so this doesn't trip
    // the unrelated "--ngrams cannot be combined with VM runtime safety
    // options" check ahead of the one under test.
    printf("ngrams:\n");
    snprintf(cmd, sizeof(cmd),
        "sh -c './cccc %s --link %s --ngrams -0 -V 2>&1; echo \"exit=$?\"'",
        app_out, lib_out);
    const char *ngrams_result = CaptureCommand(ctx, cmd);
    if (ngrams_result) puts(ngrams_result);

    printf("fusion:\n");
    snprintf(cmd, sizeof(cmd),
        "sh -c './cccc %s --link %s --fusion-candidates=10 -0 -V 2>&1; echo \"exit=$?\"'",
        app_out, lib_out);
    const char *fusion_result = CaptureCommand(ctx, cmd);
    if (fusion_result) puts(fusion_result);

    return 0;
}
