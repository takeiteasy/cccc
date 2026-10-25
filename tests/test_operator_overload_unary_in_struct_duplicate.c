// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator '-' is already defined in this struct
typedef struct W {
    int n;
    W operator-(W a) {
        return a;
    }
    W operator-(W a) {
        return a;
    }
} W;
int main(void) {
    return 0;
}
