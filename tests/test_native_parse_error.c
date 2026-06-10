// mode="native" + error= should produce a parse-time compile error.
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing

[[cccc::test(mode = "native", error = ".*")]]
void test_native_neg_invalid(void) {
}
