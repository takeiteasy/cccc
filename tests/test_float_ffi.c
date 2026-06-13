// Tests that float-returning math functions (registered with returns_double=2)
// produce correct float results via FFI (#406).
#include <math.h>

static int feq(float a, float b) {
    float diff = a - b;
    if (diff < 0.0f) diff = -diff;
    float mag = b < 0.0f ? -b : b;
    if (mag < 1e-6f) mag = 1e-6f;
    return diff / mag < 1e-5f;
}

// Wrappers for the CALLN (indirect/function-pointer) path
static float wrap_sqrtf(float x) { return sqrtf(x); }
static float wrap_hypotf(float x, float y) { return hypotf(x, y); }

int main(void) {
    // Basic float FFI via CALLF (direct, statically registered)
    if (!feq(sqrtf(16.0f), 4.0f)) return 1;
    if (!feq(fabsf(-3.5f), 3.5f)) return 2;
    if (!feq(fmodf(5.5f, 2.0f), 1.5f)) return 3;

    // Transcendental functions
    if (!feq(expf(0.0f), 1.0f)) return 4;
    if (!feq(logf(1.0f), 0.0f)) return 5;
    if (!feq(powf(2.0f, 10.0f), 1024.0f)) return 6;

    // Trig
    if (!feq(sinf(0.0f), 0.0f)) return 7;
    if (!feq(cosf(0.0f), 1.0f)) return 8;

    // Multi-arg float FFI
    if (!feq(hypotf(3.0f, 4.0f), 5.0f)) return 9;
    if (!feq(atan2f(0.0f, 1.0f), 0.0f)) return 10;

    // CALLN path: call float-returning function via function pointer
    float (*fn1)(float) = wrap_sqrtf;
    if (!feq(fn1(25.0f), 5.0f)) return 11;

    float (*fn2)(float, float) = wrap_hypotf;
    if (!feq(fn2(5.0f, 12.0f), 13.0f)) return 12;

    return 42;
}
