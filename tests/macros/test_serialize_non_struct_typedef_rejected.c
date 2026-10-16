// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @serialize expects a struct/union target

@serialize typedef int Number;

int main(void) {
    return 42;
}
