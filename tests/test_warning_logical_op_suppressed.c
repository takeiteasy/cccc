// CCCC_FLAGS: -Wno-logical-op
// CCCC_REJECT_STDERR: logical-op

int main(void) {
    int x = 1;
    if (x && 1)
        x = 2;
    return 42;
}
