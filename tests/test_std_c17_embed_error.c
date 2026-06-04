// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -std=c17
// JCC_EXPECT_STDERR: '#embed' is not available before C23
int main(void) {
    unsigned char data[] = {
        #embed "embed_data/test_data.bin"
    };
    return 0;
}
