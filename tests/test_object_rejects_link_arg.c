// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=object --link-arg=-lm
// CCCC_EXPECT_STDERR: -c=object does not link

int main(void) {
    return 42;
}
