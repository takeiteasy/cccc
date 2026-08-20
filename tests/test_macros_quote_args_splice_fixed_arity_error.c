// EXPECT_COMPILE_ERROR
// Test ticket #286: $@k splice that expands to the wrong number of arguments
// for a fixed-arity callee must produce a compile-time error after expansion.

int add3(int a, int b, int c) {
    return a + b + c;
}

int add2(int a, int b) {
    return a + b;
}

// Too few: splice 2 nodes into a 3-parameter callee.
[[cccc::comptime]]
Node *too_few_splice(Node *a, Node *b) {
    Node *chain = __builtin_node_list((Node *[]){a, b}, 2);
    return __builtin_quote("add3($@1)", chain);
}

int main(void) {
    return too_few_splice(1, 2);
}
