// CCCC_FLAGS: --build --build-out-dir=build/test_parkg_out --build-jobs=2 --build-keep-going
// CCCC_EXPECT_STDERR: target 'badbuild' failed, continuing
// CCCC_EXPECT_STDOUT: build failed \(1 error\)
// CCCC_EXPECT_STDOUT: skipped:
//
// Verify parallel keep-going behaviour (#557): a failed target's dependent is
// skipped (not attempted) and reported in the summary as "skipped:", while an
// independent target still succeeds.  Failure count must reflect only the
// directly-failed target, not the skipped dependents.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *bad = RunCustom(ctx, "badbuild", "false");

    BuildTarget *dep = Executable(ctx, "dep_on_bad");
    AddSource(dep, "examples/build_demo/src/main.c");
    AddInclude(dep, "examples/build_demo/include");
    DependsOn(dep, bad);

    BuildTarget *indep = Executable(ctx, "indep");
    AddSource(indep, "examples/build_demo/src/greet.c");
    AddInclude(indep, "examples/build_demo/include");

    return BuildDefault(ctx);
}
