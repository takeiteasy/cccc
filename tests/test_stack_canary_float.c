// CCCC_FLAGS: --stack-canaries
// #445 — float/double params (the float_param_mask spill branch in ENT3) must
// land in the canary-shifted slots too.

double fadd(double a, double b) {
    return a + b;
}

// Mixed int + float params with a local.
float mix(int n, float f) {
    float local = (float)n + f;
    return local * 2.0f;
}

int main(void) {
    if (fadd(20.5, 21.5) != 42.0)
        return 1;
    if (mix(10, 1.0f) != 22.0f) // (10 + 1) * 2
        return 2;
    return 42;
}
