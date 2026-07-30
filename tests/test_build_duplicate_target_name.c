// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: duplicate target name 'dup'
//
// #842: two-pass builds (e.g. pass1/pass2 compiling the same output name
// against different generated inputs) rely on target names being unique --
// find_target_by_name() returns the first match, and duplicate names would
// silently share the same build/obj/<name> objdir. new_target() must reject
// a second target declared with an already-used name.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *a = Executable(ctx, "dup");
    AddSource(a, "examples/build_demo/src/main.c");
    BuildTarget *b = StaticLib(ctx, "dup");
    AddSource(b, "examples/build_demo/src/lib/sum.c");
    return BuildDefault(ctx);
}
