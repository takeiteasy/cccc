// CCCC_FLAGS: -Wswitch
// CCCC_EXPECT_STDERR: enumeration value 'C' not handled in switch.*\[-Wswitch\]

typedef enum { A, B, C } Color;

int describe(Color c) {
    switch (c) {
        case A: return 1;
        case B: return 2;
    }
    return 0;
}

int main(void) {
    (void)describe(A);
    return 42;
}
