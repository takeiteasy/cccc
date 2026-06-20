// Ticket #531: backtick interpolation uses CALLF stack overflow arguments.

[[cccc::comptime(inline)]]
Node *sum10(Node *a, Node *b, Node *c, Node *d, Node *e,
            Node *f, Node *g, Node *h, Node *i, Node *j) {
    return `${a} + ${b} + ${c} + ${d} + ${e} +
            ${f} + ${g} + ${h} + ${i} + ${j}`;
}

int main(void) {
    return sum10(4, 4, 4, 4, 4, 4, 4, 4, 5, 5);
}
