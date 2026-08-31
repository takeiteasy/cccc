// EXPECT_COMPILE_ERROR
// Ticket #1242: an ND_QUOTE_LAZY node that is never spliced into anything
// that materialises it (via a $N splice site or FunctionSetBody) must be a
// clear compile-time error rather than silently reaching add_type/codegen
// with a meaningless type. Here a comptime macro returns a QuoteLazy()
// result directly into expression position, with no splice site to
// materialise it.

[[cccc::comptime]]
Node *bad(void) {
    return QuoteLazy("1 + 1");
}

int main(void) {
    int v = bad();
    return v;
}
