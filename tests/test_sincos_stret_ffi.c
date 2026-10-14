// Expected return: 42
// macOS libm's struct-returning __sincos*_stret entry points (what the SDK's
// __sincos/__sincospi header inlines call) are callable from the VM.
#ifdef __APPLE__
struct d2 {
    double s, c;
};
struct f2 {
    float s, c;
};
extern struct d2 __sincos_stret(double);
extern struct d2 __sincospi_stret(double);
extern struct f2 __sincosf_stret(float);
extern struct f2 __sincospif_stret(float);

int main(void) {
    struct d2 a = __sincos_stret(0.0);
    struct d2 b = __sincospi_stret(0.5);
    struct f2 c = __sincosf_stret(0.0f);
    struct f2 d = __sincospif_stret(0.5f);
    if (a.s != 0.0 || a.c != 1.0)
        return 1;
    if (b.s != 1.0 || b.c > 1e-9 || b.c < -1e-9)
        return 2;
    if (c.s != 0.0f || c.c != 1.0f)
        return 3;
    if (d.s != 1.0f)
        return 4;
    return 42;
}
#else
int main(void) {
    return 42;
}
#endif
