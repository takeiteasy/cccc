// CCCC_FLAGS: -Wimplicit-function-declaration
// CCCC_EXPECT_STDERR: implicit declaration of function 'puts'
// \[-Wimplicit-function-declaration\]
int main(void) {
    puts("implicit FFI declaration");
    return 42;
}
