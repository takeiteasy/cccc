// EXPECT_COMPILE_ERROR
// Recursive macro expansion should report a compiler error, not crash.

[[cccc::comptime(inline)]]
Node *recurse_forever(void) {
    return Quote("recurse_forever()");
}

int main(void) {
    return recurse_forever();
}
