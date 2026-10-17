// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDERR: AddLdFlag is not supported on a CcccObject target
//
// A CcccObject is not linked and takes no host-cc flags, so AddLdFlag on it is a
// declaration-time error.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *other = CcccObject(ctx, "cccc_object_reject_other");
    AddSource(other, "examples/build_demo/src/lib/sum.c");
    BuildTarget *obj = CcccObject(ctx, "cccc_object_reject_addldflag");
    AddSource(obj, "examples/build_demo/src/lib/sum.c");
    AddLdFlag(obj, "-lm");
    return BuildDefault(ctx);
}
