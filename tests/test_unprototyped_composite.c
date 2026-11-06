// CCCC_FLAGS: --std=c17 -Wnonnull
// CCCC_EXPECT_STDERR: returns_nonnull
int g();
int h();
int rev(double d, int i);
int k();

int h(double d, int i);
int rev();

__attribute__((returns_nonnull)) int *r();

int g(double d, int i) {
    return (int)d + i;
}
int h(double d, int i) {
    return (int)d + i;
}
int k(a, d)
int    a;
double d;
{
    return a + (int)d;
}
int rev(double d, int i) {
    return (int)d + i;
}

int *r(int a) {
    static int z;
    if (a)
        return 0;
    return &z;
}

int main(void) {
    if (g(2, 40) != 42)
        return 1;
    if (h(2, 40) != 42)
        return 2;
    if (rev(2, 40) != 42)
        return 3;
    if (k(40, 2.0) != 42)
        return 4;
    if (!r(0))
        return 5;
    return 42;
}
