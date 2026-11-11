// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'template' must be the only attribute in its list

[[cccc::template(T), nodiscard]] T id(T a) {
    return a;
}

int main(void) {
    return 0;
}
