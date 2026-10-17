// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: ar rcs .*libcccc_object_ar\.a .*cccc_object_ar_sum\.o
//
// A CcccObject linked into a StaticLib is added to the archive.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *obj = CcccObject(ctx, "cccc_object_ar_sum");
    AddSource(obj, "examples/build_demo/src/lib/sum.c");

    BuildTarget *lib = StaticLib(ctx, "cccc_object_ar");
    AddSource(lib, "examples/build_demo/src/greet.c");
    AddInclude(lib, "examples/build_demo/include");
    LinkWith(lib, obj);
    return BuildDefault(ctx);
}
