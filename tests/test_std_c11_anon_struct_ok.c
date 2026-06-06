// JCC_FLAGS: --std=c11
struct Outer {
    int x;
    struct { int a; int b; };
};
int main(void) {
    struct Outer o;
    o.x = 1; o.a = 2; o.b = 3;
    return o.a == 2 ? 42 : 1;
}
