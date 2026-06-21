// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: gen/generated\.c
//
// #542: AddSourceStr writes inline source content to <out_dir>/gen/<name> and
// adds it to the target's compile list.  Verified via dry-run: the generated
// path (containing "gen/generated.c") must appear in the cc command line.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *lib = StaticLib(ctx, "genlib");
    AddSourceStr(lib, "generated.c",
        "int generated_add(int a, int b) { return a + b; }\n");

    return BuildDefault(ctx);
}
