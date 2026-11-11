// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'template' is a cccc attribute; write \[\[cccc::template\]\]

[[gnu::template(T)]] T id(T a) {
    return a;
}

int main(void) {
    return 0;
}
