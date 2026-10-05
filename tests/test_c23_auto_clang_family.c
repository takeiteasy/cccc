// CCCC_FLAGS: --compiler-family=clang -Werror=auto-declarator
//
// A pointer declarator and several declarators on one `auto` or `__auto_type`
// declaration draw no -Wauto-declarator warning when modelling clang.

int main(void) {
    int   x = 99;
    auto *q = &x;
    if (*q != 99)
        return 1;
    *q = 100;
    if (x != 100)
        return 2;

    auto **pp = &q;
    if (**pp != 100)
        return 3;

    auto ai = 1, bd = 2.0;
    if (sizeof(ai) != sizeof(int) || sizeof(bd) != sizeof(double))
        return 4;

    __auto_type *r = &x;
    if (*r != 100)
        return 5;

    __auto_type ci = 1, cd = 2.0;
    if (sizeof(ci) != sizeof(int) || sizeof(cd) != sizeof(double))
        return 6;

    return 42;
}
