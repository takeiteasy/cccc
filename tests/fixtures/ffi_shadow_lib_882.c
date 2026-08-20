// Fixture for tests/test_build_bytecode_link_ffi_shadow.c (#882).
//
// Defines `abs` -- a name that is also a registered FFI symbol (see
// <stdlib.h>) -- with a guest body that is trivially distinguishable from
// the real libc abs(): it always returns 42 regardless of its argument.
// Compiled standalone to a .c4a bytecode library; a bare declaration in
// another module is expected to resolve to *this* definition once linked
// via --link, not silently fall back to the host FFI abs().
int abs(int x) {
    return 42;
}
