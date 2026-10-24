// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator '\*' is already defined in this struct
typedef struct V {
    int x;
    V operator *(V a, int k) {
        return a;
    }
    V operator *(int k, V a) {
        return a;
    }
} V;
int main(void) {
    return 0;
}
