// CCCC_FLAGS: -Wconversion -Wno-conversion
// CCCC_REJECT_STDERR: warning:

int narrow(int x) {
    char c = x;
    return c;
}

int main(void) {
    long big   = 100000L;
    int  small = big;
    return narrow(small) != 100000 ? 42 : 0;
}
