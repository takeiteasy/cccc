// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'auto' requires a plain identifier, possibly with attributes, as declarator
int main(void) {
    int   a = 1;
    auto *p = &a;
    return *p;
}
