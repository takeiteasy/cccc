// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'template' needs a parameter list

[[cccc::template]] int one(void) {
    return 1;
}

int main(void) {
    return 0;
}
