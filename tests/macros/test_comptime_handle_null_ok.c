// A NULL handle and handles the compiler returned are accepted.
[[cccc::comptime]]
Node *probe(void) {
    Node *items[2] = {MakeIntLiteral(40), MakeIntLiteral(2)};
    Node *list     = __builtin_node_list(items, 2);
    if (TokenFromNode(NULL) != NULL || !list)
        return MakeIntLiteral(1);
    return MakeIntLiteral(42);
}

int main(void) {
    return probe();
}
