// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot overload '\|\|'
typedef struct V {
    int x;
    int operator || (V a, V b) {
        return 1;
    }
} V;
int main(void) {
    return 0;
}
