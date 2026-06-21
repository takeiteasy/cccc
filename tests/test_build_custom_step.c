// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: \(custom\) echo hello from custom step
// CCCC_EXPECT_STDOUT: build succeeded
//
// #544: RunCustom registers a shell-command as a DAG node.  DependsOn wires an
// ordering edge from the executable to the custom step without adding a -l flag.
// Verified via dry-run: the custom command line must appear in the output and
// the link step must NOT contain -lgen (DependsOn is order-only, not link-with).

[[cccc::build]]
int build_main(Builder *ctx) {
    // Custom codegen step
    BuildTarget *gen = RunCustom(ctx, "gen", "echo hello from custom step");

    // Executable depends on gen for ordering (DependsOn), not linking (LinkWith)
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, "examples/build_demo/include");
    DependsOn(app, gen);

    return BuildDefault(ctx);
}
