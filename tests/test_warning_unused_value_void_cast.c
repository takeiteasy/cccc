// CCCC_FLAGS: -Wunused-value
// CCCC_REJECT_STDERR: expression result unused

int main(void) {
    int x = 1, y = 2;
    (void)(x + y);
    return 42;
}
