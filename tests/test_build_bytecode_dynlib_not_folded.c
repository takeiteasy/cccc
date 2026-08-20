// CCCC_FLAGS: --build --build-dry-run --build-target=bc_exe_with_plugin
// CCCC_EXPECT_STDOUT: \.c4d
//
// Regression test for DynamicLib(kind=bytecode) not being folded into the
// depending bytecode executable (#564).  A dynamic module is loaded at runtime
// (via cc_load_module), not at compile time; the build system must build it as
// a standalone .c4d, not fold its sources into the exe's cccc invocation.
//
// The dry-run output must contain a .c4d path (the plugin is built separately),
// and the exe's cccc invocation must NOT contain plugin.c (not folded).

[[cccc::build_target(kind = bytecode)]]
BuildTarget *bc_exe_with_plugin(Builder *ctx) {
    // Dynamic module: no main(), loaded at runtime.
    BuildTarget *plugin = DynamicLib(ctx, "plugin");
    AddSource(plugin, "tests/fixtures/build_bytecode_libs_demo/src/plugin.c");

    // Executable: declares extern symbols, loads plugin at runtime.
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "tests/fixtures/build_bytecode_libs_demo/src/main.c");
    AddInclude(app, "tests/fixtures/build_bytecode_libs_demo/include");
    // DependsOn ensures the plugin is built before the exe, but does NOT fold.
    DependsOn(app, plugin);
    return app;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
