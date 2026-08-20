// #817 (mined from clang's Sema/switch.c test coverage): a case label whose
// value doesn't correspond to any enumerator of the switch's enum-typed
// condition should be flagged -- the reverse of the existing "enumeration
// value not handled in switch" check.
// CCCC_FLAGS: -Wswitch
// CCCC_EXPECT_STDERR: case value not in enumerated type.*\[-Wswitch\]

typedef enum { A = 1, B } Color;

int describe(Color c) {
    switch (c) {
        case A:
            return 1;
        case B:
            return 2;
        case 3:
            return 3;
        default:
            return 0;
    }
}

int main(void) {
    (void)describe(A);
    return 42;
}
