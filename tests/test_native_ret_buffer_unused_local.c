// A return buffer for an unused local is not materialized under -c=native.
struct Pair {
    int a;
    int b;
};
struct Pair mk_19(int a, int b) {
    struct Pair p;
    p.a = a;
    p.b = b;
    return p;
}
struct Pair wrap_tail_19(void) {
    return mk_19(1, 2);
}
int use_init_19(void) {
    struct Pair p = mk_19(3, 4);
    return p.a + p.b;
}
int use_subexpr_19(void) {
    struct Pair p;
    p = mk_19(5, 6);
    return p.a + p.b;
}
int main(void) {
    struct Pair w = wrap_tail_19();
    if (w.a != 1 || w.b != 2)
        return 1;
    if (use_init_19() != 7)
        return 2;
    if (use_subexpr_19() != 11)
        return 3;
    return 42;
}
