// A user @comptime(attribute("tag")) name is a cccc attribute: -E drops it
// whatever namespace it was written in.
// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: struct +A \{
// CCCC_EXPECT_STDOUT: struct +B \{
// CCCC_EXPECT_STDOUT: struct +C \{
// CCCC_EXPECT_STDOUT: \[\[nodiscard\]\] int f\(void\);
// CCCC_REJECT_STDOUT: tag
// CCCC_REJECT_STDOUT: __attribute__
@comptime(attribute("tag")) void tag_handler(AttrTarget *t) {
    (void)t;
}
struct [[tag]] A {
    int x;
};
struct [[gnu::tag]] B {
    int x;
};
struct __attribute__((tag)) C {
    int x;
};
[[nodiscard, tag]] int f(void);
int main(void) {
    return 42;
}
