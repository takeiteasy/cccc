// Build script for the demo project. Run with:
//
//     cccc --build examples/build_demo/build.c
//
// It declares three native targets — a static library, a dynamic library, and
// an executable that links against both — and builds them with the system
// toolchain. The same file is an ordinary C source: the [[cccc::build]] entry
// is only invoked in --build mode.
//
// Also demonstrates:
//   - AddSourcesGlob / ExcludeSource  (#542)
//   - AddSourceStr                    (#542)
//   - HaveTool / PkgConfig            (#543)
//   - RunCustom / DependsOn           (#544)

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *inc = "examples/build_demo/include";

    // --- core static library ---
    BuildTarget *core = StaticLib(ctx, "core");
    // Use glob to pick up all sources under src/lib/ (#542)
    AddSourcesGlob(core, "examples/build_demo/src/lib/*.c");
    AddInclude(core, inc);

    // --- greet dynamic library ---
    BuildTarget *greet = DynamicLib(ctx, "greet");
    // Glob the whole src/ directory then exclude main.c (#542)
    AddSourcesGlob(greet, "examples/build_demo/src/*.c");
    ExcludeSource(greet, "examples/build_demo/src/main.c");
    AddInclude(greet, inc);

    // --- optional: a generated source via AddSourceStr (#542) ---
    // (Writes examples/build_demo/src/version.c under <out_dir>/gen/)
    AddSourceStr(core, "version.c",
                 "const char *build_version(void) { return \"demo\"; }\n");

    // --- optional: probe for zlib and use it if available (#543) ---
    // if (HaveTool(ctx, "pkg-config") && PkgConfig(greet, "zlib") == 0)
    //     AddDefine(greet, "HAVE_ZLIB", NULL);

    // --- custom codegen step (#544) ---
    // Runs before the core library is compiled (DependsOn = ordering only).
    BuildTarget *gen =
        RunCustom(ctx, "gen-headers",
                  "echo '/* generated */' > examples/build_demo/include/gen.h");
    DependsOn(core,
              gen); // core waits for gen-headers; no -lgen-headers linker flag

    // --- main executable ---
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);
    AddDefine(app, "GREET_DEFAULT", "\"build mode\"");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault(ctx);
}
