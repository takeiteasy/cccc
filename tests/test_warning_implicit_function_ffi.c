// CCCC_FLAGS: --std=c89 -Wimplicit-function-declaration
// #1144: implicit function declaration is a hard error at C99+ (and always
// under -c=native); --std=c89 is what keeps this a warning to test.
// CCCC_EXPECT_STDERR: implicit declaration of function 'puts'
// \[-Wimplicit-function-declaration\]
int main(void) {
    puts("implicit FFI declaration");
    return 42;
}
