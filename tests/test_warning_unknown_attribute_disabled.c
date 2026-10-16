// CCCC_FLAGS: -Wno-unknown-attributes
// CCCC_REJECT_STDERR: unknown attribute
int x @no_such_handler;

int main(void) {
    return 42;
}
