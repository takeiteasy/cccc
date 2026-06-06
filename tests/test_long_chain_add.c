// Regression test for ticket #295: long left-associative binary chains
// exhausted the fixed temp-register pool (11 regs, T0-T10).

static int one(void) { return 1; }

// 20-operand int addition chain — well past the old limit of 12.
static int long_int_add(int a, int b, int c, int d, int e, int f, int g,
                        int h, int i, int j, int k, int l, int m, int n,
                        int o, int p, int q, int r, int s, int t) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
}

// Subtraction chain — verifies non-commutative ops stay correct.
static int long_int_sub(int a, int b, int c, int d, int e, int f) {
    return a - b - c - d - e - f;
}

// Chain with a function call as one operand — exercises the rhs_has_call
// push/pop path together with a long LHS chain.
static int chain_with_call(int a, int b, int c, int d, int e, int f,
                           int g, int h, int i, int j, int k, int l) {
    return a + b + c + d + e + f + g + h + i + j + k + l + one();
}

// 20-operand float addition chain — exercises the float branch.
static float long_float_add(float a, float b, float c, float d, float e,
                            float f, float g, float h, float i, float j,
                            float k, float l, float m, float n, float o,
                            float p, float q, float r, float s, float t) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
}

int main(void) {
    // 1+2+...+20 = 210
    int sum = long_int_add(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20);
    if (sum != 210) return 1;

    // 100 - 1 - 2 - 3 - 4 - 5 = 85
    int diff = long_int_sub(100, 1, 2, 3, 4, 5);
    if (diff != 85) return 2;

    // 1+2+...+12+1 = 79
    int mixed = chain_with_call(1,2,3,4,5,6,7,8,9,10,11,12);
    if (mixed != 79) return 3;

    // 1.0+2.0+...+20.0 = 210.0
    float fsum = long_float_add(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20);
    if ((int)fsum != 210) return 4;

    return 42;
}
