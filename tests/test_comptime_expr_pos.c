// Test: [[cccc::comptime]] (without inline) functions are now callable in
// expression position, not just at file scope.
[[cccc::comptime]]
Node *ct_add(Node *a, Node *b) {
    return MakeBinary(NK_ADD, a, b);
}

int main(void) {
    int v = ct_add(20, 22);
    return v == 42 ? 42 : 1;
}
