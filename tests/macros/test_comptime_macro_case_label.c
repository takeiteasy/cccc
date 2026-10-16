// A [[cccc::comptime]] Node* macro that returns an integer expression is a
// constant in a case label (including a GNU range).

[[cccc::comptime]]
Node *K(int unused_marker) {
    return MakeIntLiteral(7);
}

[[cccc::comptime]]
Node *PLUS(Node *a, Node *b) {
    return MakeBinary(NK_ADD, a, b);
}

static int classify(int v, int before) {
    int local_a = before + 1;
    int result  = -1;
    int local_b = local_a * 2;
    switch (v) {
        case K(0):
            result = 1;
            break;
        case PLUS(20, 2):
            result = 2;
            break;
        case PLUS(100, 0)... PLUS(100, 9):
            result = 3;
            break;
        default:
            result = 0;
            break;
    }
    return result + local_b - local_b;
}

int main(void) {
    if (classify(7, 0) != 1)
        return 1;
    if (classify(22, 5) != 2)
        return 2;
    if (classify(104, 0) != 3)
        return 3;
    if (classify(5, 0) != 0)
        return 4;
    return 42;
}
