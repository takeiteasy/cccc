// Test finite nested macro expansion under the default recursion limit.

[[cccc::comptime(inline)]]
Node *finish_step(Node *x) {
    return MakeBinary(NK_ADD, x, MakeIntLiteral(1));
}

[[cccc::comptime(inline)]]
Node *start_step(Node *x) {
    return Quote("finish_step($1)", x);
}

int main(void) {
    return start_step(41);
}
