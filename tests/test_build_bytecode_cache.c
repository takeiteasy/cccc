// CCCC_FLAGS: --build --build-out-dir=build/test_bytecode_cache_out --build-cache --build-target=bc_cached
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// Smoke test for --build-cache with kind=bytecode targets (#562): the runner
// must enable incremental caching without errors and the build must succeed.

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_cached(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "bc_cached_app");
    AddSource(t, "examples/build_demo/src/main.c");
    AddSource(t, "examples/build_demo/src/greet.c");
    AddSource(t, "examples/build_demo/src/lib/sum.c");
    AddInclude(t, "examples/build_demo/include");
    return t;
}

[[cccc::build]]
int build_main(Builder *ctx) { return 0; }
