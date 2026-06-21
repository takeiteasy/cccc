// EXPECT_COMPILE_ERROR
// kind=bytecode is reserved for the bytecode linker (#545) and must be rejected
// at compile time with a clear error message.

[[cccc::build_target(kind=bytecode)]]
BuildTarget *my_target(Builder *ctx) {
    return Executable(ctx, "app");
}
