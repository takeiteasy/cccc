// CCCC_FLAGS: --std=c17
int sum(a, b)
int a, b;
{
    return a + b;
}

double mixed(x, p, n)
double x, *p;
int    n;
{
    return x + *p + n;
}

int arr(c, s)
char c, *s;
{
    return c + s[0];
}

int main(void) {
    if (sum(40, 2) != 42)
        return 1;
    double d = 10.5;
    if (mixed(20.5, &d, 1) != 32.0)
        return 2;
    if (arr(2, "(") != 42)
        return 3;
    return 42;
}
