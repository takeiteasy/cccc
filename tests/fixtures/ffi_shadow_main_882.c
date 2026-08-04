// Fixture for tests/test_build_bytecode_link_ffi_shadow.c (#882).
//
// Declares `abs` (no body -- an ordinary bare declaration, exactly the
// shape a system header would produce) and exercises both call shapes that
// resolve a bodiless callee: a direct call (the call-patch pass,
// ffi_index_for_callee) and a function-pointer address-of (the separate
// func_addr_patches pass) -- #882's fix touches both. If either resolves to
// the host FFI abs() instead of ffi_shadow_lib_882.c's guest definition,
// abs(-1) contributes 1 instead of 42 and the sum below is not 42.
int abs(int x);
int main(void) {
    int (*fp)(int) = abs;
    return fp(-1) + abs(-1) - 42;
}
