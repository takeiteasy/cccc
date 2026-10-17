// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: --compile=object -o .*obj/cccc_object_dry\.o
// CCCC_EXPECT_STDOUT: -DX=1
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
// CCCC_REJECT_STDOUT: --compile=native
// CCCC_REJECT_STDOUT:  -l
//
// A CcccObject lowers to one `cccc --compile=object` step that carries the
// compile-side options only.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *obj = CcccObject(ctx, "cccc_object_dry");
    AddSource(obj, "examples/build_demo/src/lib/sum.c");
    AddInclude(obj, "examples/build_demo/include");
    AddDefine(obj, "X", "1");
    return BuildDefault(ctx);
}
