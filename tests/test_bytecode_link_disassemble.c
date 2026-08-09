// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: === Disassembly ===[\s\S]*SAME
// CCCC_REJECT_STDOUT: CALL\s+0(\s|$)
//
// Regression/coverage for #903: fixing #898 moved the bytecode linker pass
// to run once, immediately after codegen, before every terminal sink --
// including --disassemble. Previously --disassemble sat before the old
// --link handling and always disassembled the *unlinked* image, with any
// cross-module CALL left at its unresolved placeholder operand of 0. Now it
// disassembles the fully linked image, same as -o/--testing/the plain run
// (see man/BUILDING.md, "The --link compiler flag").
//
// This locks that contract down two ways:
//   1. `cccc main.c --link lib.c4a --disassemble` must be byte-identical to
//      disassembling the .c4 that the equivalent `-o` invocation writes --
//      the --disassemble sink must see exactly what -o sees.
//   2. No resolved cross-module CALL site may be left at operand 0 (the
//      unpatched relocation placeholder); text instructions start at pc 1
//      (text_seg[0] is the entry-point offset), so "CALL 0" can only ever
//      be an unresolved relocation, never a legitimate call target.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char lib_out[512], app_out[512], disasm_direct[512], disasm_from_file[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/mathlib_link_disasm_test.c4a", out_dir);
    snprintf(app_out, sizeof(app_out), "%s/bin/app_link_disasm_test.c4", out_dir);
    snprintf(disasm_direct, sizeof(disasm_direct), "%s/link_disasm_direct.txt", out_dir);
    snprintf(disasm_from_file, sizeof(disasm_from_file), "%s/link_disasm_from_file.txt", out_dir);

    // Build a library.
    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/lib && ./cccc "
        "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include "
        "--compile=bytecode -o %s 2>&1",
        out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    // Disassemble directly from source + --link (no -o).
    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/bin && ./cccc "
        "tests/fixtures/build_bytecode_libs_demo/src/main.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include --link %s "
        "--disassemble > %s 2>&1",
        out_dir, lib_out, disasm_direct);
    if (!CaptureCommand(ctx, cmd)) return 1;

    // Write the fully linked .c4 via -o, then disassemble *that* file.
    snprintf(cmd, sizeof(cmd),
        "./cccc tests/fixtures/build_bytecode_libs_demo/src/main.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include --link %s -o %s 2>&1",
        lib_out, app_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    snprintf(cmd, sizeof(cmd), "./cccc %s --disassemble > %s 2>&1",
        app_out, disasm_from_file);
    if (!CaptureCommand(ctx, cmd)) return 1;

    snprintf(cmd, sizeof(cmd), "diff -q %s %s >/dev/null 2>&1 && echo SAME || echo DIFFERENT",
        disasm_direct, disasm_from_file);
    const char *diff_result = CaptureCommand(ctx, cmd);

    snprintf(cmd, sizeof(cmd), "cat %s", disasm_direct);
    const char *disasm_output = CaptureCommand(ctx, cmd);

    if (disasm_output) puts(disasm_output);
    if (diff_result) puts(diff_result);
    return 0;
}
