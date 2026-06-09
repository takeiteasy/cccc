/* CCCC_FLAGS: --std=c89 -Wpedantic */
/* CCCC_EXPECT_STDERR: warning: compound literals are a C99 extension \[-Wpedantic\] */
int f(void) {
    int *p = (int []){1, 2, 3};
    return p[0] == 1 ? 42 : 1;
}

int main(void) {
    return f();
}
