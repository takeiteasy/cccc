// CCCC_FLAGS: -Wcast-align -Wno-cast-align
// CCCC_REJECT_STDERR: warning:

int main(void) {
    char buf[4] = {0};
    char *cp = buf;
    int *ip = (int *)cp;
    (void)ip;
    return 42;
}
