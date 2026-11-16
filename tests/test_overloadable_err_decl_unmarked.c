// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: declaration of 'f' must have the 'overloadable' attribute

int f(int) __attribute__((overloadable));
float f(float);
float f(double);

int main(void) {
    return 0;
}
