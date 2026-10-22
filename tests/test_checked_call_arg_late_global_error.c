// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'limit'
// A bound binds to the globals declared before the prototype, never to one
// declared after it.

void sink(int *[[cccc::array, cccc::count(limit)]] p);

int limit = 4;

int main(void) {
    return 42;
}
