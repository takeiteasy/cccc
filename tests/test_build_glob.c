// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: main\.c
// CCCC_REJECT_STDOUT: greet\.c
//
// #542: AddSourcesGlob expands a glob pattern into the source list; ExcludeSource
// removes a matching file.  Verified via dry-run: the cc command for main.c must
// appear but greet.c must be absent (excluded).

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *lib = StaticLib(ctx, "demo");
    // Glob matches both greet.c and main.c under src/
    AddSourcesGlob(lib, "examples/build_demo/src/*.c");
    // Exclude greet.c — should be absent from the dry-run output
    ExcludeSource(lib, "examples/build_demo/src/greet.c");
    AddInclude(lib, "examples/build_demo/include");

    return BuildDefault(ctx);
}
