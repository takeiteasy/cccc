// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator function 'V_operator_mul' must take two parameters
typedef struct V {
    int x;
    V   operator * (V a) {
        return a;
    }
} V;
int main(void) {
    return 0;
}
