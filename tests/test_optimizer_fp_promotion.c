// CCCC_FLAGS: --optimize=3

// double accumulator loop (mandelbrot-style)
static double sum_double(int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += (double)i * 0.5;
    return s;
}

// float accumulator loop
static float sum_float(int n) {
    float s = 0.0f;
    for (int i = 0; i < n; i++)
        s += (float)i * 0.5f;
    return s;
}

// double param: not initialised locally — promoted from stack slot
static double scale(double x, int n) {
    for (int i = 0; i < n; i++)
        x *= 2.0;
    return x;
}

// multiple double locals competing for slots
static double multi(int n) {
    double a = 1.0, b = 2.0, c = 3.0;
    for (int i = 0; i < n; i++) {
        a += b;
        b += c;
        c += a;
    }
    return a + b + c;
}

int main(void) {
    // sum_double(100) == 0+0.5+1+...+49.5 == 2475.0
    if (sum_double(100) != 2475.0)
        return 1;

    // sum_float result should match within float precision
    float sf = sum_float(100);
    if (sf < 2474.0f || sf > 2476.0f)
        return 2;

    // scale(1.0, 10) == 2^10 == 1024.0
    if (scale(1.0, 10) != 1024.0)
        return 3;

    // multi: a,b,c start 1,2,3; after 5 iters check it's deterministic
    double m0 = multi(5);
    double m1 = multi(5);
    if (m0 != m1)
        return 4;
    if (m0 <= 0.0)
        return 5;

    return 42;
}
