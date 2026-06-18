// CCCC_FLAGS: --fma --optimize=4
// Verifies correctness of the FMSUB3_FMA / FMSUB3_F32_FMA single-rounding path.
// Uses powers-of-2 so the result is exactly representable and agrees with the
// two-rounding default — no observable divergence for these inputs.

static double dot_sub_d(const double *a, const double *b, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum -= a[i] * b[i];
    return sum;
}

static float dot_sub_f(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++)
        sum -= a[i] * b[i];
    return sum;
}

int main(void) {
    // Powers of 2 are exactly representable — FMA and two-rounding agree.
    double a[3] = {1.0, 2.0, 4.0};
    double b[3] = {1.0, 2.0, 4.0};
    // -(1 + 4 + 16) = -21
    if (dot_sub_d(a, b, 3) != -21.0)
        return 1;

    float fa[3] = {1.0f, 2.0f, 4.0f};
    float fb[3] = {1.0f, 2.0f, 4.0f};
    if (dot_sub_f(fa, fb, 3) != -21.0f)
        return 2;

    return 42;
}
