// EXPECT_RUNTIME_ERROR
// CCCC_FLAGS: --optimize=3

int main(void) {
    return 42 / (6 - 6);
}
