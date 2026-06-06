// EXPECT_COMPILE_ERROR
// Recursive macro expansion should report a compiler error, not crash.

[[jcc::comptime(inline)]]
$node_t *recurse_forever(void) {
    return $quote("recurse_forever()");
}

int main(void) {
    return recurse_forever();
}
