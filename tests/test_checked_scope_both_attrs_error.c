// EXPECT_COMPILE_ERROR
// Checked regions (#485): 'checked' and 'unchecked' cannot both apply to the
// same function definition -- apply_checked_scope_attr() (src/parse_types.c)
// rejects the conflict at parse time.

[[cccc::checked, cccc::unchecked]]
void f(void) {}

int main(void) {
    f();
    return 42;
}
