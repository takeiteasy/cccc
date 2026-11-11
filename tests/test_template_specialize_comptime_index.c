@template(T) struct Vec {
    T a;
};
[[cccc::comptime]] int f(void) {
    return 1;
}
@specialize(int) struct Vec {
    int a, b;
};
int main(void) {
    @inst(int) Vec v = {1, 2};
    return v.b == 2 ? 42 : 1;
}
