// CCCC_FLAGS: --std=c17
int a(int);
int a(const int x);
int b(void);
int b() {
    return 0;
}
int c();
int c(f)
float f;
{
    return (int)f;
}
int d(float);
int d(f)
float f;
{
    return (int)f;
}
int e();
int e(double, int);
int f(f2)
char f2;
{
    return f2;
}
int f(int);
int g();
int g();
int h(char *);
int h(char *p);
int i(int, ...);
int i(int a, ...);

int a(int x) {
    return x;
}
int e(double d, int n) {
    return (int)d + n;
}
int h(char *p) {
    return *p;
}
int i(int n, ...) {
    return n;
}

int main(void) {
    if (a(1) != 1 || e(2.0, 40) != 42 || f(3) != 3 || c(5.0f) != 5)
        return 1;
    return 42;
}
