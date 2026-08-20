// CCCC_FLAGS: -2
// #982 (defect D): a partial VLA brace initializer bypasses
// lvar_initializer entirely (create_vla_init/var_definition build the
// alloca + init directly, src/parse.c), so it never got
// lvar_initializer's pre-zero step for unspecified elements. At the
// default safety level a fresh heap block happens to already be zero, so
// the gap was invisible -- but -2/-3's CCCC_MEMORY_POISONING fills every
// fresh allocation with 0xCD before use (vm_heap_bump_alloc_ex, src/
// ops.c), so an *omitted* element read back as 0xCDCDCDCD instead of 0.
// Asserted here by reading the omitted element's actual value, not just a
// pass/fail boolean, so this test cannot pass by accident the way a bare
// `== 0` comparison against an already-zero default-level block might.
int main(void) {
    int n = 2, m = 2;
    int v[n][m] = {{1, 2}}; // second row omitted entirely
    int omitted = v[1][1];
    return omitted == 0 ? 42 : (omitted == (int)0xCDCDCDCD ? 2 : 1);
}
