// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: comparison operator function 'V_operator_eq' must return an integer
typedef struct V {
    int x;
    V   operator == (V a, V b) {
        return a;
    }
} V;
int main(void) {
    return 0;
}
