// By-value vector function arguments and returns (tracker #714, follow-up to
// #72). Vectors are passed/returned by memory, reusing the struct-by-value
// ABI (pointer-passing for args via a caller-side scratch copy, RETBUF
// rotating pool for returns) rather than the register FReg/GReg convention --
// a 128-bit vector doesn't fit an 8-byte arg slot. See VM.md.

typedef float v4sf __attribute__((vector_size(16)));
typedef int   v4si __attribute__((vector_size(16)));

// Vector param read.
static float sum4(v4sf v) {
    return v[0] + v[1] + v[2] + v[3];
}

// Vector return.
static v4sf make(float a, float b, float c, float d) {
    v4sf v;
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    return v;
}

// Vector param + vector return together, chained calls.
static v4sf scale2(v4sf v) {
    v4sf two;
    two[0] = 2.0f;
    two[1] = 2.0f;
    two[2] = 2.0f;
    two[3] = 2.0f;
    return v * two;
}

// By-value mutation: writing to the param must not affect the caller's copy.
static void mutate(v4sf v) {
    v[0] = 999.0f;
}

// int-lane vector param/return.
static v4si iadd1(v4si v) {
    v4si one;
    one[0] = 1;
    one[1] = 1;
    one[2] = 1;
    one[3] = 1;
    return v + one;
}

// 9 args so the vector (last) is pushed on the stack (args 8+).
static float sum4_after8(int a, int b, int c, int d, int e, int f, int g, int h,
                         v4sf v) {
    return (float)(a + b + c + d + e + f + g + h) + v[0] + v[1] + v[2] + v[3];
}

// Tail-position vector-returning call (exercises the CALLT rejection --
// must still compute correctly via a plain, non-tail CALL).
static v4sf tail_make(float a, float b, float c, float d) {
    return make(a, b, c, d);
}

// Tail-position call taking a vector arg.
static float tail_sum(v4sf v) {
    return sum4(v);
}

int main(void) {
    v4sf a;
    a[0] = 1.0f;
    a[1] = 2.0f;
    a[2] = 3.0f;
    a[3] = 4.0f;

    // Vector param read.
    if (sum4(a) != 10.0f)
        return 1;

    // Vector arg from an rvalue expression (a + a).
    if (sum4(a + a) != 20.0f)
        return 2;

    // Vector return.
    v4sf b = make(10.0f, 20.0f, 30.0f, 40.0f);
    if (b[0] != 10.0f)
        return 3;
    if (b[3] != 40.0f)
        return 4;

    // Chained calls: param + return together.
    v4sf c = scale2(b);
    if (c[0] != 20.0f)
        return 5;
    if (c[3] != 80.0f)
        return 6;

    // f(g()) chaining -- RETBUF rotation must keep both buffers distinct.
    v4sf d = scale2(make(1.0f, 2.0f, 3.0f, 4.0f));
    if (d[0] != 2.0f)
        return 7;
    if (d[3] != 8.0f)
        return 8;

    // By-value mutation: caller's original must be unchanged after the call.
    v4sf orig;
    orig[0] = 5.0f;
    orig[1] = 6.0f;
    orig[2] = 7.0f;
    orig[3] = 8.0f;
    mutate(orig);
    if (orig[0] != 5.0f)
        return 9;

    // int-lane vector param/return.
    v4si vi;
    vi[0]    = 1;
    vi[1]    = 2;
    vi[2]    = 3;
    vi[3]    = 4;
    v4si vi2 = iadd1(vi);
    if (vi2[0] != 2)
        return 10;
    if (vi2[3] != 5)
        return 11;
    // Original int vector unchanged (by-value semantics).
    if (vi[0] != 1)
        return 12;

    // Vector arg pushed on the stack (9th arg).
    v4sf s;
    s[0] = 0.5f;
    s[1] = 0.5f;
    s[2] = 0.5f;
    s[3] = 0.5f;
    if (sum4_after8(1, 2, 3, 4, 5, 6, 7, 8, s) != 38.0f)
        return 13;

    // Tail-position vector-returning call.
    v4sf t = tail_make(100.0f, 200.0f, 300.0f, 400.0f);
    if (t[0] != 100.0f)
        return 14;
    if (t[3] != 400.0f)
        return 15;

    // Tail-position call taking a vector arg.
    if (tail_sum(a) != 10.0f)
        return 16;

    // Discarded vector return: dest_reg is REG_ZERO in the result-tail VLDR
    // path (codegen.c ND_FUNCALL). Must not crash or corrupt other state.
    make(1.0f, 1.0f, 1.0f, 1.0f);
    if (sum4(a) != 10.0f)
        return 17; // 'a' must be unaffected by the above

    return 42;
}
