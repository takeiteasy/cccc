// A `[[gnu::unavailable]]` call is rejected (gcc spells it in `gnu::`).
// EXPECT_COMPILE_ERROR
[[gnu::unavailable]] int gone(void);
int main(void) {
    return gone();
}
