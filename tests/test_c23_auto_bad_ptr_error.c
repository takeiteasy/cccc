// EXPECT_COMPILE_ERROR
int main(void) {
    auto *p = 5; // int is not a pointer
    return *p;
}
