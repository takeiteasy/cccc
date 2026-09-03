// CCCC_FLAGS: --build --build-dry-run --std=c23
// CCCC_EXPECT_STDOUT: -std=c2
//
// #1274: push_compile_flags() (src/build.c) used to forward `--std=` to a
// --build native target's compiler byte-for-byte, with no probe -- so a
// build script run with `--std=c23` on a host cc that rejects strict
// `-std=c23` but accepts `-std=c2x` (e.g. GCC 13, the exact asymmetry #1073
// fixed for the plain -c=native driver) failed the target with a confusing
// host-compiler error instead of a CCCC one.
//
// Fixed by routing --build targets through the same probed ladder (src/
// exec.c's cccc_resolve_host_std()), restricted to alias spellings of the
// NAMED standard, with the user's own c/gnu prefix preserved (unlike
// -c=native, a --build target compiles the user's own source, so there is
// no "always prefer gnu" reason to widen the dialect). Verified via dry-run
// (no compiler invoked) so this test needs no wrapper script: the assertion
// only checks that SOME `c23`/`c2x` spelling is what gets forwarded, which
// is host-portable regardless of which one the local host cc actually
// prefers.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "examples/build_demo/src/main.c");
    return Build(ctx, t);
}
