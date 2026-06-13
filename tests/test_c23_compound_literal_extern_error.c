// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
int main(void) {
    int *p = &(extern int){ 7 };
    return *p;
}
