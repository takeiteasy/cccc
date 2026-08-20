// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @shared is only valid on #include or #define
// @shared / [[cccc::shared]] must be rejected on directives other than
// #include and #define (#888 added #define @shared as a second valid
// directive; #undef is still rejected).
#undef @shared FOO
int main(void) {
    return 0;
}
