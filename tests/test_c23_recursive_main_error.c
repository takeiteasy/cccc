// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too many arguments
// In C23 (default), int main() is equivalent to int main(void).
// A recursive call with arguments must be a hard error.
int main() {
    main(23, "elephants");
    return 0;
}
