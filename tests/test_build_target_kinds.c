// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_EXPECT_STDOUT: -shared
//
// Dry-run all three target kinds (exe, static, dynamic) in one graph.
// Asserts that `ar rcs` appears for the static lib and `-shared` for the
// dynamic lib — confirming the correct toolchain verb per kind.

[[cccc::build]]
int build_main(void) {
    cccc_target_t *slib = StaticLib("mystat");
    AddSource(slib, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *dlib = DynamicLib("mydyn");
    AddSource(dlib, "examples/build_demo/src/greet.c");

    cccc_target_t *exe = Executable("myexe");
    AddSource(exe, "examples/build_demo/src/main.c");

    return BuildAll();
}
