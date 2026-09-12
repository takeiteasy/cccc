// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int f\(void\)
// CCCC_REJECT_STDOUT: _Checked
//
// #1331 -- `_Checked { ... }` carries no Type/Obj/Node state of its own (like
// the [[cccc::checked]] attribute it mirrors), so it must not appear in
// -m/-c=native/-c=generated output: it is consumed entirely by the parser.
// See tests/test_checked_scope_serialize_strip.c for the attribute-form
// equivalent.

int f(void) {
    _Checked {
        int *[[cccc::array, cccc::count(1)]] a = 0;
        (void)a;
        return 42;
    }
}

int main(void) {
    return f();
}
