// A Quote() template instantiates function and struct templates.
@template(T) T max(T a, T b) {
    return a > b ? a : b;
}
@template(T) struct Box {
    T v;
};
@template(T) T twice(T x) {
    return x + x;
}

@comptime Node *biggest(Node *a, Node *b) {
    return Quote("@inst(int) max($1, $2)", a, b);
}

@comptime Node *box_value(Node *v) {
    return Quote("({ @inst(long) Box b = { $1 }; b.v; })", v);
}

@comptime Node *double_it(Node *x) {
    return Quote("@inst(int) twice($1)", x);
}

int main(void) {
    int m = biggest(7, 12);              // 12
    int b = (int)box_value(20);          // 20
    int d = double_it(5) + double_it(5); // 20, one instance shared
    return m + b + d - 10;
}
