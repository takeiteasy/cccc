// CCCC_REJECT_STDERR: unknown attribute
// Only the @ spelling is reported by default.
int x [[no_such_handler]];
int y __attribute__((no_such_handler));

int main(void) {
    return 42;
}
