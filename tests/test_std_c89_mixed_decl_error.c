/* CCCC_FLAGS: --std=c89 -Wpedantic */
/* CCCC_EXPECT_STDERR: warning: mixing declarations and code is a C99 extension \[-Wpedantic\] */
int f(void) {
    int x = 1;
    x = 2;
    int y = 3;
    return x + y == 5 ? 42 : 1;
}

int main(void) {
    return f();
}
