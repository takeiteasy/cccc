// CCCC_FLAGS: --std=c17
int  g();
int  many();
int  id();
double scale();

int pick(void) { return 3; }

int main(void) {
    if (g(2.5, 40) != 42)
        return 1;
    if (g(2.5f, 40) != 42)
        return 2;
    if (many(1, 2, 3, 4, 5, 6, 7, 8, 9.5, 10, 11.5, 12) != 42)
        return 3;
    if (g(pick() - 0.5, 40) != 42)
        return 4;
    if (id(40, scale(1.5, 2)) != 43)
        return 5;
    return 42;
}

int g(double d, int i) { return (int)d + i; }

int many(int a, int b, int c, int d, int e, int f, int g2, int h, double x,
         int y, double z, int w) {
    return (int)(x + z) + (a + b + c + d + e + f + g2 + h) - 36 + y + w - 1;
}

int id(int a, double b) { return a + (int)b; }

double scale(double a, int b) { return a * b; }
