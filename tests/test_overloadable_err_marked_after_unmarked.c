// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: declaration of 'h' must not have the 'overloadable' attribute

int h(int);
int h(int) __attribute__((overloadable));

int main(void) {
    return 0;
}
