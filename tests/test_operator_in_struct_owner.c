// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator '\+' in V needs a V or V \* operand
typedef struct O {
    int y;
} O;
typedef struct V {
    int x;
    O   operator + (O a, O b) {
        return a;
    }
} V;
int main(void) {
    return 0;
}
