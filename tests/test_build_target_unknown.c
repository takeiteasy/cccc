// CCCC_FLAGS: --build --build-target=nope
// CCCC_EXPECT_STDERR: does not match any declared target
//
// --build-target=NAME with a name that does not exist must print an error
// that names the unknown target and lists available targets.

[[cccc::build]]
int build_main(void) {
    cccc_target_t *t = Executable("real");
    AddSource(t, "examples/build_demo/src/main.c");
    return Build(t);
}
