// CCCC_FLAGS: -Wint-conversion
// CCCC_EXPECT_STDERR: incompatible integer to pointer conversion.*\[-Wint-conversion\]

int main(void) {
    const char *p = 'a';
    (void)p;
    return 42;
}
