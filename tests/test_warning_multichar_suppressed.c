// CCCC_FLAGS: -Wno-multichar
// CCCC_REJECT_STDERR: multi-character character constant

int main(void) {
    int x = 'ab';
    (void)x;
    return 42;
}
