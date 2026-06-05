// Test native f32 register arithmetic and raw-bit save/restore

static float add_half(float x) {
    return x + 0.5f;
}

static float call_add_half(float x) {
    return add_half(x);
}

static float sum3(float a, float b, float c) {
    return a + b + c;
}

static int check_args(float a, float b, float c) {
    float x = sum3(a, call_add_half(b), c);
    return x == 42.0f;
}

int main() {
    float lhs = 16.0f;
    float combined = lhs + call_add_half(25.5f);
    if (combined != 42.0f)
        return 1;

    if (!check_args(10.0f, 20.5f, 11.0f))
        return 2;

    float rounded = 16777216.0f + 1.0f;
    if (rounded != 16777216.0f)
        return 3;

    float neg = -rounded;
    if (neg >= 0.0f)
        return 4;

    return 42;
}
