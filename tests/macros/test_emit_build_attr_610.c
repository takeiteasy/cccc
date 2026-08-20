// CCCC_FLAGS: --build
// Ticket #610: [[cccc::build]] functions inside emit blocks must be registered
// and invoked by the build runner.

#pragma cccc comptime begin
#pragma cccc emit begin
[[cccc::build]]
int emitted_build_fn(Builder *ctx) {
    return 42;
}
#pragma cccc emit end
#pragma cccc comptime end
