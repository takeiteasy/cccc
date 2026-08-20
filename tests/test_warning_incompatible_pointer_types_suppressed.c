// CCCC_FLAGS: -Wincompatible-pointer-types -Wno-incompatible-pointer-types
// CCCC_REJECT_STDERR: warning:

int main(void) {
    int   x  = 42;
    int  *ip = &x;
    char *cp = ip;
    (void)cp;
    return 42;
}
