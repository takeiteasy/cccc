// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(4 targets, 0 errors\)
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_EXPECT_STDOUT: -shared
// CCCC_EXPECT_STDOUT: --compile=native
//
// Dry-run all four target kinds (exe, static, dynamic, cccc executable) in
// one graph. Asserts `ar rcs` for the static lib, `-shared` for the dynamic
// lib, and `--compile=native` for the CcccExecutable — confirming the
// correct backend per kind.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *slib = StaticLib(ctx, "mystat");
    AddSource(slib, "examples/build_demo/src/lib/sum.c");

    BuildTarget *dlib = DynamicLib(ctx, "mydyn");
    AddSource(dlib, "examples/build_demo/src/greet.c");

    BuildTarget *exe = Executable(ctx, "myexe");
    AddSource(exe, "examples/build_demo/src/main.c");

    BuildTarget *cexe = CcccExecutable(ctx, "myccccexe");
    AddSource(cexe, "examples/build_demo/src/main.c");

    return BuildAll(ctx);
}
