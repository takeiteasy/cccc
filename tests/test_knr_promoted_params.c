// CCCC_FLAGS: --std=c17
#include <stddef.h>

int g();
int (*gp)();
int h(double);
int f32(float);
int nar();
int (*np)();

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
    return 42;
}
