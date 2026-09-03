// CCCC_FLAGS: --build --build-dry-run --std=c99 --build-cc=/usr/bin/false
// CCCC_EXPECT_STDERR: --std=c99: compiler '/usr/bin/false' accepts none of the -std= spellings
//
// #1274: push_compile_flags() (src/build.c) used to forward an explicit
// `--std=` byte-for-byte with no probe, so a --build target whose compiler
// honours no spelling of the named standard failed with a confusing
// host-compiler parse error deep inside the first source file, rather than
// a plain CCCC diagnostic naming the compiler and the spellings tried.
//
// /usr/bin/false is used as a stand-in "compiler" that accepts nothing at
// all -- present on every macOS/Linux CI host, and, since the probe is a
// `-fsyntax-only` spawn, rejects every spelling the same way a genuinely
// incompatible real compiler would. Verified via dry-run: this must fail
// before compile_sources() ever spawns the (bogus) compiler for real.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "examples/build_demo/src/main.c");
    return Build(ctx, t);
}
