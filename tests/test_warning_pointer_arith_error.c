// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=pointer-arith
// CCCC_EXPECT_STDERR: error: pointer of type 'void \*' used in
// arithmetic.*\[-Wpointer-arith\]

int main(void) {
    char  buf[4] = {1, 2, 3, 4};
    void *p      = buf;
    void *q      = p + 2;
    return *((char *)q);
}
