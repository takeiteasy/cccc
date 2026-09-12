// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #486: `_Dynamic_check` is the Checked-C-compat alias for
// __builtin_cccc_dynamic_check, recognized positionally (an identifier not
// currently in scope, immediately followed by '(') in primary()
// (src/parse_postfix.c).

int main(void) {
    int i = 10, n = 5;
    _Dynamic_check(i < n);
    return 0;
}
