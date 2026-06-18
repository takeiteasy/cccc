// CCCC_FLAGS: --optimize=4

// multiply-accumulate loop — exercises FMUL3+FADD3 -> FMADD3 fusion
static double dot_d(const double *a, const double *b, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

// float variant — exercises FMUL3_F32+FADD3_F32 -> FMADD3_F32 fusion
static float dot_f(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

// mandelbrot-style inner loop: z = z*z + c
static int mandel_iters(double cr, double ci, int max) {
    double zr = 0.0, zi = 0.0;
    for (int i = 0; i < max; i++) {
        double zr2 = zr * zr - zi * zi + cr;
        double zi2 = zr * zi + zr * zi + ci;  // 2*zr*zi+ci (two FMUL+FADD chains)
        zr = zr2;
        zi = zi2;
        if (zr2 * zr2 + zi2 * zi2 > 4.0)
            return i;
    }
    return max;
}

int main(void) {
    double a[4] = {1.0, 2.0, 3.0, 4.0};
    double b[4] = {5.0, 6.0, 7.0, 8.0};
    // 1*5 + 2*6 + 3*7 + 4*8 = 5+12+21+32 = 70
    if (dot_d(a, b, 4) != 70.0)
        return 1;

    // result must match manually computed value (two-rounding correctness)
    double manual = (1.0 * 5.0) + (2.0 * 6.0) + (3.0 * 7.0) + (4.0 * 8.0);
    if (dot_d(a, b, 4) != manual)
        return 2;

    float fa[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float fb[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    if (dot_f(fa, fb, 4) != 70.0f)
        return 3;

    // mandelbrot: (0,0) is in the set — should return max
    if (mandel_iters(0.0, 0.0, 100) != 100)
        return 4;

    // (2,2) escapes quickly
    if (mandel_iters(2.0, 2.0, 100) != 0)
        return 5;

    return 42;
}
