// Ticket #288: $@k initializer splices can mix with ordinary positional
// elements and inferred array lengths.

struct Triple {
    int a;
    int b;
    int c;
};

[[cccc::comptime]]
Node *make_triple_tail(Node *b, Node *c) {
    Node *chain = __builtin_node_list((Node *[]){b, c}, 2);
    return Quote("(struct Triple){ 1, $@1 }", chain);
}

[[cccc::comptime]]
Node *make_triple_middle(Node *a, Node *b) {
    Node *chain = __builtin_node_list((Node *[]){a, b}, 2);
    return Quote("(struct Triple){ $@1, 30 }", chain);
}

[[cccc::comptime]]
Node *make_arr4(Node *b, Node *c) {
    Node *chain = __builtin_node_list((Node *[]){b, c}, 2);
    return Quote("(int[4]){ 1, $@1, 4 }", chain);
}

[[cccc::comptime]]
Node *make_arr_inferred(Node *a, Node *b, Node *c) {
    Node *chain = __builtin_node_list((Node *[]){a, b, c}, 3);
    return Quote("(int[]){ $@1 }", chain);
}

[[cccc::comptime]]
Node *make_arr_mixed_inferred(Node *b, Node *c) {
    Node *chain = __builtin_node_list((Node *[]){b, c}, 2);
    return Quote("(int[]){ 1, $@1, 4 }", chain);
}

int main(void) {
    struct Triple t1 = make_triple_tail(20, 21);
    if (t1.a != 1 || t1.b != 20 || t1.c != 21)
        return 1;

    struct Triple t2 = make_triple_middle(10, 2);
    if (t2.a != 10 || t2.b != 2 || t2.c != 30)
        return 2;

    int *a = make_arr4(2, 35);
    if (a[0] != 1 || a[1] != 2 || a[2] != 35 || a[3] != 4)
        return 3;

    int *b = make_arr_inferred(10, 20, 12);
    if (b[0] != 10 || b[1] != 20 || b[2] != 12)
        return 4;

    int *c = make_arr_mixed_inferred(2, 35);
    if (c[0] != 1 || c[1] != 2 || c[2] != 35 || c[3] != 4)
        return 5;

    return 42;
}
