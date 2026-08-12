// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: asm\("nop"\)
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// `asm(...)` is the one construct deliberately emitted verbatim even though
// the VM does not execute it by default (`--asm-passthru` opts into VM
// execution via the native CC + FFI). Every other node kind is serialized to
// match VM behaviour; this one cannot be, because there is no way to
// evaluate host assembly inside the VM at all. Native output therefore hands
// the asm to the host compiler and the divergence is documented rather than
// papered over. See COVERAGE.md.

int main(void) {
    asm("nop");
    return 42;
}
