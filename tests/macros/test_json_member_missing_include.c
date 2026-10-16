// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined custom attribute 'json_skip'
// CCCC_EXPECT_STDERR: <cccc/derive.h>

struct P {
    int x @json_skip;
};

int main(void) {
    return 42;
}
