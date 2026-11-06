// CCCC_FLAGS: --std=c17
#include <stddef.h>

int g();
int (*gp)();
int h(double);
int f32(float);
int nar();
int (*np)();
int nb();
int (*bp)();
int pb(_Bool);

int g(f)
float f;
{
    return (int)(f * 2.0f);
}

int h(f)
float f;
{
    return (int)f;
}

int f32(f)
float f;
{
    return (int)f;
}

int nar(c, s)
char  c;
short s;
{
    return c + s;
}

int nb(b) _Bool b;
{
    return b;
}

int pb(b) _Bool b;
{
    return b;
}

float write_back(f)
float f;
{
    float *p = &f;
    *p       = 40.5f;
    return f;
}

int main(void) {
    gp = g;
    if (g(10.5f) != 21)
        return 1;
    if (gp(99.25) != 198)
        return 2;
    if (g(1234.5) != 2469)
        return 3;
    np = nar;
    if (np(300, 70000) != 44 + 4464)
        return 7;
    if (np(1, 2) != 3)
        return 4;
    if ((int)write_back(1.0f) != 40)
        return 5;
    if (h(7.5) != 7)
        return 6;
    bp = nb;
    if (bp(2) != 1)
        return 8;
    if (bp(256) != 1)
        return 9;
    if (bp(0) != 0)
        return 10;
    if (nb(2) != 1)
        return 11;
    if (pb(2) != 1)
        return 12;
    return 42;
}
