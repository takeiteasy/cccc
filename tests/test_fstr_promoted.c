// CCCC_FLAGS: --optimize=3
// Regression test: float local promoted to FREG_S* then stored through a
// pointer (FSTR) or indexed (FSTR_INDEX). Sub-pass B must call MARK_FLOAT_USE
// for the float-source register in byte 0, and sub-pass C must count it.
// Without the fix, KILL_FLOAT_DEF incorrectly NOP'd the FMOV3 before FSTR.
#include <stdlib.h>

static double store_via_ptr(double *out, double a, double b) {
    double x = 0.0;
    for (int i = 0; i < 4; i++)
        x += a;
    x = b;        // Promoted register updated to b; previous loop value differs.
    *out = x;     // FMOV3 tmp=FREG_S0; FSTR tmp, *out — tmp must NOT be NOP'd.
    return 0.0;   // Do not return x; only consumer of the promoted-read is FSTR.
}

static float store_via_ptr_f32(float *out, float a, float b) {
    float x = 0.0f;
    for (int i = 0; i < 4; i++)
        x += a;
    x = b;
    *out = x;
    return 0.0f;
}

static double store_via_index(double *arr, double a, double b, int idx) {
    double x = 0.0;
    for (int i = 0; i < 4; i++)
        x += a;
    x = b;
    arr[idx] = x;  // indexed store: FSTR_INDEX
    return 0.0;
}

int main(void) {
    double d_out;
    store_via_ptr(&d_out, 1.0, 99.0);
    if (d_out != 99.0)
        return 1;

    float f_out;
    store_via_ptr_f32(&f_out, 1.0f, 88.0f);
    if (f_out != 88.0f)
        return 2;

    double arr[4] = {0};
    store_via_index(arr, 1.0, 77.0, 2);
    if (arr[2] != 77.0)
        return 3;

    return 42;
}
