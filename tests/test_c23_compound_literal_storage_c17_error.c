// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
int main(void) {
    int *p = &(static int){ 7 };
    return *p;
}
