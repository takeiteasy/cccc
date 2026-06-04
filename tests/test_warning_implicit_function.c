// JCC_FLAGS: -Wimplicit-function-declaration
// JCC_EXPECT_STDERR: 1 warning generated.
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
