/* EXPECT_COMPILE_ERROR */
/* CCCC_FLAGS: --std=c89 -Werror=pedantic */
/* CCCC_EXPECT_STDERR: error: compound literals are a C99 extension \[-Wpedantic\] */
int main(void) {
    int *p = (int []){42};
    return p[0];
}
