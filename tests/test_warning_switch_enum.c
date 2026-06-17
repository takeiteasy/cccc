// CCCC_FLAGS: -Wswitch-enum
// CCCC_EXPECT_STDERR: enumeration value 'C' not handled in switch.*\[-Wswitch-enum\]

typedef enum { A, B, C } Color;

int describe(Color c) {
    switch (c) {
        case A: return 1;
        case B: return 2;
        default: return 0;
    }
}

int main(void) {
    (void)describe(A);
    return 42;
}
