// CCCC_FLAGS: -Wno-switch-default
// CCCC_REJECT_STDERR: switch statement has no default case

int classify(int x) {
    switch (x) {
        case 1: return 1;
        case 2: return 2;
    }
    return 0;
}

int main(void) {
    (void)classify(1);
    return 42;
}
