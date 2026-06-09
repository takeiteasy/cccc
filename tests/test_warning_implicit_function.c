// CCCC_FLAGS: -Wimplicit-function-declaration
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
