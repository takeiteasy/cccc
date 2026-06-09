// CCCC_FLAGS: -Wsign-compare -Wno-sign-compare
// CCCC_REJECT_STDERR: warning:

int main(void) {
    int x = -1;
    unsigned int y = 1u;
    int result = (x < y) ? 1 : 0;
    return result == 0 ? 42 : 0;
}
