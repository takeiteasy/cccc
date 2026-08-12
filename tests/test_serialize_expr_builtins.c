// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_frame_address\(0\)
// CCCC_EXPECT_STDOUT: __builtin_dynamic_object_size\(
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// Frame/return address, trap and dynamic object size pass straight back
// through to the identical host builtin. Two of them diverge in *value*
// rather than in spelling, by design (see COVERAGE.md):
//
//   - `__builtin_return_address` yields a VM bytecode pc under the VM and a
//     real host return address natively — both are "the return address n
//     frames up" in their own runtime, which is the closest mapping there is.
//   - `__builtin_dynamic_object_size` reads the VM's own allocation header,
//     so the VM always knows the exact size; the host builtin answers its
//     documented "unknown" ((size_t)-1) unless the optimizer can see the
//     allocation.
//
// `__builtin_unreachable`, `__builtin_trap` and `__builtin_debugtrap` all
// lower to the same BTRAP opcode, so the original spelling is not
// recoverable at serialization time. `__builtin_trap()` is emitted for all
// three because it is the one that matches what the VM does — emitting
// `__builtin_unreachable()` would be undefined behaviour natively and the
// host optimizer would delete the path.

int trapper(int x) {
    if (x > 0)
        return 42;
    __builtin_unreachable();
}

// Only the emitted *spelling* is asserted for the size query, deliberately:
// the value legitimately differs between the two runtimes, so a native
// run-and-compare would encode the host optimizer's setting rather than
// anything about the serializer.
unsigned long remaining(char *p) {
    return __builtin_dynamic_object_size(p, 0);
}

int main(void) {
    void *fp = __builtin_frame_address(0);
    void *ra = __builtin_return_address(0);
    char buf[64];
    (void)ra;
    (void)remaining(buf);
    return fp ? trapper(1) : 1;
}
