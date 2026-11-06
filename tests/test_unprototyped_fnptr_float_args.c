// CCCC_FLAGS: --std=c17
double sqrt();
int  (*fp)();

int g(double d, int i) { return (int)d + i; }

int main(void) {
    fp = g;
    if (fp(2.5, 40) != 42)
        return 1;
    double (*p)() = sqrt;
    if ((int)p(1764.0) != 42)
        return 2;
    return 42;
}
