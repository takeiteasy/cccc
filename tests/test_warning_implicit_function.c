// CCCC_FLAGS: --std=c89 -Wimplicit-function-declaration
// #1144: implicit function declaration is a hard error at C99+ (and always
// under -c=native); --std=c89 is what keeps this a warning to test.
// CCCC_EXPECT_STDERR: 1 warning generated.
int first(void) {
    return later();
}

int second(void) {
    return later();
}

int main(void) {
    return first() + second();
}

int later(void) {
    return 21;
}
