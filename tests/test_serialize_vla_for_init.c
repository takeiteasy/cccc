// CCCC_FLAGS: -m
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: variable-length array declared in a for-loop initializer
//
// #964: a VLA declared in a for-loop initializer parses and runs fine in
// the VM, but the init clause is serialized as comma-joined *assignments*
// (#927) and C forbids mixing a declaration with expressions there.
// Hoisting the declaration ahead of the loop would change its scope/
// lifetime and can read a variable the init clause itself assigns, so it
// is rejected with a diagnostic under -m/-c=native rather than silently
// emitting a declaration where an expression belongs. (Doing this properly
// is tracked as a follow-up.)

int main(void) {
    int n = 4;
    for (int i = 0, v[n]; i < 1; i++) {
        v[0] = 42;
        return v[0];
    }
    return 0;
}
