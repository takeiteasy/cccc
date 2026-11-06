// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'g'
int g();

void f(void) {
    int g(double, int);
}

int g(a, b)
int a;
int b;
{
    return a + b;
}

int main(void) {
    return 42;
}
