// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int f\(void\)
// CCCC_REJECT_STDOUT: checked
//
// Checked regions (#485): [[cccc::checked]]/[[cccc::unchecked]] must be
// stripped from -m/-c=native/-c=generated output, ABI-transparently, the
// same way the six checked-pointer attributes are (see
// test_checked_pointers_serialize_strip.c). Neither attribute carries any
// Type/Obj/Node state, so there is nothing for the native serializer to
// reproduce -- this pins that down as a regression test rather than relying
// on it being true by construction.

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(1)]] a = 0;
    (void)a;
    return 42;
}

int main(void) {
    return f();
}
