// CCCC_FLAGS: --std=c17
// CCCC_NATIVE_SKIP: the host compiler rejects an unprototyped call with fewer arguments than the definition has
int g();

void f(void) {
    int g(int, int);
}

int main(void) {
    if (0)
        g(1);
    return g(40, 2);
}

int g(a, b)
int a;
int b;
{
    return a + b;
}
