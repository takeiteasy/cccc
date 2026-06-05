// EXPECT_RUNTIME_ERROR
// JCC_FLAGS: --optimize=3

int main(void) {
    return 42 / (6 - 6);
}
