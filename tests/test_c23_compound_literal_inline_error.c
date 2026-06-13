// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
int main(void) {
    int *p = &(inline int){ 7 };
    return *p;
}
