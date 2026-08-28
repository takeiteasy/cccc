// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __asm__\("nop"\)
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// `asm(...)` is the one construct deliberately emitted verbatim even though
// the VM does not execute it by default (`--asm-passthru` opts into VM
// execution via the native CC + FFI). Every other node kind is serialized to
// match VM behaviour; this one cannot be, because there is no way to
// evaluate host assembly inside the VM at all. Native output therefore hands
// the asm to the host compiler and the divergence is documented rather than
// papered over. See NATIVE.md.
//
// #1130: emitted as __asm__(...), not bare asm(...) -- asm is a GNU
// alternate keyword GCC disables under a strict ISO -std=cNN, which would
// make the emitted C a syntax error there; __asm__ is accepted in every
// dialect.

int main(void) {
    asm("nop");
    return 42;
}
