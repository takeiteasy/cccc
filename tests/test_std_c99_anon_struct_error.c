// JCC_FLAGS: -std=c99 -Wpedantic
// JCC_EXPECT_STDERR: warning: anonymous structs/unions are a C11 extension \[-Wpedantic\]
struct Outer {
    int x;
    struct { int a; int b; };
};

int main(void) {
    struct Outer o;
    o.a = 40;
    o.b = 2;
    return o.a + o.b;
}
