// CCCC_FLAGS: --build --build-dry-run --build-target=bc_app
// CCCC_EXPECT_STDOUT: cccc[^\n]*answer\.c[^\n]*\.c4
//
// Regression test for kind=bytecode LinkWith (#563): a bytecode target linking
// a library (no main) must fold the library's sources into its own single cccc
// invocation, not build the library standalone.
//
// The discriminating regex requires that answer.c appears on the same line as
// the .c4 output (i.e. folded into the cccc bytecode compile), not on a
// separate native cc -c line (which is what happens before this fix).

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_app(Builder *ctx) {
    // Library target: defines answer(), no main().  Folded into app.
    BuildTarget *lib = Executable(ctx, "answer_lib");
    AddSource(lib, "tests/fixtures/build_linkwith_demo/src/answer.c");
    AddInclude(lib, "tests/fixtures/build_linkwith_demo/include");

    // Executable target: calls answer() from lib.
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "tests/fixtures/build_linkwith_demo/src/main.c");
    AddInclude(app, "tests/fixtures/build_linkwith_demo/include");
    LinkWith(app, lib);
    return app;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
