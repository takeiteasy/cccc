// CCCC_FLAGS: tests/fixtures/vendored_macro_outside_1308_smoke_a.c
// A vendored header whose configuration macro is defined outside the including
// TU builds under -c=native.
int vendored_macro_outside_1308_smoke_use(void);
int main(void) {
    return vendored_macro_outside_1308_smoke_use();
}
