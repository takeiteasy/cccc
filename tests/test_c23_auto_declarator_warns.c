// CCCC_FLAGS: -Wauto-declarator
// CCCC_EXPECT_STDERR: 'auto' requires a plain identifier, possibly with attributes, as declarator in gcc
//
// The forms gcc rejects are accepted, each declarator inferring its own type.
int main(void) {
    int  x = 99;
    auto *q = &x;
    auto ai = 1, bd = 2.0;
    if (*q != 99 || sizeof(ai) != sizeof(int) || sizeof(bd) != sizeof(double))
        return 1;
    return 42;
}
