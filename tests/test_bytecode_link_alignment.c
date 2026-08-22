// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: exit=42
//
// Regression test for #1136: cc_load_module's data_shift/tls_shift
// re-anchoring (src/bytecode.c) must preserve a module's own declared
// >8-byte alignment when appending its data segment into a host VM whose
// own data segment isn't empty. Uses tests/fixtures/build_bytecode_libs_demo
// 's align_lib.c/align_main.c: the lib places an _Alignas(32) int and an
// __int128 relative to *its own* data_seg[0]; align_main.c writes an
// odd-sized global of its own first, so the host's data_ptr is not a
// multiple of 32 (or even 16) before --link appends the lib -- if
// data_shift weren't rounded up, the re-anchored addresses would land
// misaligned even though both the lib and the host, compiled standalone,
// look correct in isolation.
//
// build_main uses CaptureCommand to invoke cccc directly for each step
// (compile the lib to .c4a, then run the main source directly with --link
// and no -o, exactly like test_bytecode_link_run_inmemory.c's #898 repro
// shape) so the test is self-contained on a clean build.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        lib_out[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/alignlib_link_test.c4a",
             out_dir);

    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s/lib && ./cccc "
             "tests/fixtures/build_bytecode_libs_demo/src/align_lib.c "
             "-I tests/fixtures/build_bytecode_libs_demo/include "
             "--compile=bytecode -o %s 2>&1",
             out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd))
        return 1;

    snprintf(cmd, sizeof(cmd),
             "sh -c './cccc "
             "tests/fixtures/build_bytecode_libs_demo/src/align_main.c "
             "-I tests/fixtures/build_bytecode_libs_demo/include --link %s "
             ">/dev/null 2>&1; echo \"exit=$?\"'",
             lib_out);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
