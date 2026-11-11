// --emit-cccc keeps a user comptime attribute as [[cccc::name]].
// CCCC_FLAGS: --emit-cccc -E
// CCCC_EXPECT_STDOUT: struct \[\[cccc::tag\]\] A \{
// CCCC_EXPECT_STDOUT: struct \[\[cccc::tag\]\] B \{
// CCCC_REJECT_STDOUT: __attribute__\(\(tag
// CCCC_REJECT_STDOUT: gnu::
@comptime(attribute("tag")) void tag_handler(AttrTarget *t) {
    (void)t;
}
struct [[tag]] A {
    int x;
};
struct [[gnu::tag]] B {
    int x;
};
int main(void) {
    return 42;
}
