// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// Smoke test for --build mode: declares a static lib, a dynamic lib, and an
// executable linking both, then dry-runs the host runner (prints the toolchain
// command lines without spawning cc/ar/ld). Exercises topo-sort, all three
// target kinds, dependency linking and define forwarding deterministically.

[[cccc::build]]
int build_main(void) {
    cccc_target_t *core = StaticLib("core");
    AddSource(core, "src/lib/sum.c");
    AddInclude(core, "include");

    cccc_target_t *greet = DynamicLib("greet");
    AddSource(greet, "src/greet.c");
    AddInclude(greet, "include");

    cccc_target_t *app = Executable("app");
    AddSource(app, "src/main.c");
    AddInclude(app, "include");
    AddDefine(app, "GREET_DEFAULT", "\"world\"");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault();
}
