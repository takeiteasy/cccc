// CCCC_FLAGS: -Wswitch-default
// CCCC_EXPECT_STDERR: switch statement has no default
// case.*\[-Wswitch-default\]

int classify(int x) {
    switch (x) {
        case 1:
            return 1;
        case 2:
            return 2;
    }
    return 0;
}

int main(void) {
    (void)classify(1);
    return 42;
}
