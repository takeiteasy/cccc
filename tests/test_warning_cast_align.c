// CCCC_FLAGS: -Wcast-align
// CCCC_EXPECT_STDERR: cast increases required alignment.*\[-Wcast-align\]

int main(void) {
    char  buf[4] = {0};
    char *cp     = buf;
    int  *ip     = (int *)cp;
    (void)ip;
    return 42;
}
