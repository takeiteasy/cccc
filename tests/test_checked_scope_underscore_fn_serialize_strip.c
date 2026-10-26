// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int f\(void\)
// CCCC_EXPECT_STDOUT: int g\(void\)
// CCCC_REJECT_STDOUT: _Checked
// CCCC_REJECT_STDOUT: _Unchecked
//
// The function-level keywords carry no Type/Obj/Node state, so neither the
// suffix nor the specifier spelling reaches -m/-c=native/-c=generated output.

int f(void) _Checked {
    return 20;
}

_Unchecked int g(void) {
    return 22;
}

int main(void) {
    return f() + g();
}
