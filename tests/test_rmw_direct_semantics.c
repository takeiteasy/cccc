// ++, -- and op= on plain variables of every kind lower to `x = x op y`.
int             g  = 5;
static long     sg = 7;
volatile int    vg = 1;
static unsigned ug = 0;

int main(void) {
    g++;
    g += 4;
    if (g != 10)
        return 1;
    if (sg-- != 7 || --sg != 5)
        return 2;
    vg  += 2;
    vg <<= 3;
    if (vg != 24)
        return 3;
    ug--;
    if (ug != 0xffffffffu)
        return 4;

    char c = 127;
    c++;
    if (c != -128)
        return 5;
    unsigned char uc  = 255;
    uc               += 2;
    if (uc != 1)
        return 6;
    short s  = 3;
    s       *= 5;
    s       %= 4;
    s       ^= 6;
    if (s != 5)
        return 7;

    int  a[4]  = {1, 2, 3, 4};
    int *p     = a;
    p         += 2;
    if (*p != 3 || *p++ != 3 || *p != 4 || *--p != 3)
        return 8;
    p -= 2;
    if (p != a)
        return 9;

    double d  = 1.5;
    d        *= 2;
    d++;
    float f  = 0.25f;
    f       += 0.25f;
    if (d != 4.0 || f != 0.5f)
        return 10;

    int x = 3;
    int y = (x += 2, x -= 1);
    if (y != 4 || x != 4)
        return 11;
    return 42;
}
