// CCCC_FLAGS: --build --build-dry-run --build-target=emitted_factory
// CCCC_EXPECT_STDOUT: emitted_source\.c
// Ticket #610: [[cccc::build_target]] inside emit blocks must be registered
// and selectable via --build-target.
//
// Executable/AddSource are preprocessor macros, so we use their underlying
// builtins directly inside the emit block where macro expansion is bypassed.

typedef struct BuildTarget BuildTarget;
typedef struct Builder     Builder;
BuildTarget *__builtin_build_executable(Builder *ctx, const char *name);
void __builtin_build_add_source(BuildTarget *t, const char *path);

#pragma cccc comptime begin
#pragma cccc emit begin
[[cccc::build_target]]
BuildTarget *emitted_factory(Builder *ctx) {
    BuildTarget *t = __builtin_build_executable(ctx, "emitted_app");
    __builtin_build_add_source(t, "emitted_source.c");
    return t;
}
#pragma cccc emit end
#pragma cccc comptime end
