// CCCC_FLAGS: -Wincompatible-pointer-types
// CCCC_EXPECT_STDERR: incompatible pointer
// types.*\[-Wincompatible-pointer-types\]

int main(void) {
    int   x  = 42;
    int  *ip = &x;
    char *cp = ip;
    (void)cp;
    return 42;
}
