// #1137/#1448: 32- and 64-byte vector locals/params get an address aligned to
// _Alignof (the host ABI's vector alignment) under the VM and -c=native, at
// every bp parity.
typedef float v8f32 __attribute__((vector_size(32)));
typedef float v16f32 __attribute__((vector_size(64)));

static int misaligned(void *p, int align) {
    return (unsigned long long)p % (unsigned long long)align != 0;
}

static int locals(int n) {
    char   pad     = (char)n;
    v8f32  v32     = {1, 2, 3, 4, 5, 6, 7, 8};
    char   pad2[3] = {1, 2, 3};
    v16f32 v64     = {1};
    if (misaligned(&v32, __alignof__(v8f32)))
        return 1;
    if (misaligned(&v64, __alignof__(v16f32)))
        return 2;
    return v32[7] == 8 && v64[0] == 1 && pad == (char)n && pad2[2] == 3 ? 0 : 3;
}

static int param(int a, v8f32 w, v16f32 x) {
    if (misaligned(&w, __alignof__(v8f32)))
        return 1;
    if (misaligned(&x, __alignof__(v16f32)))
        return 2;
    return w[7] == 8 && x[0] == 1 ? 0 : 3;
}

static int stacky(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int n) {
    return locals(n + (a + b + c + d + e + f + g + h + i) * 0);
}

static int stacky10(int a, int b, int c, int d, int e, int f, int g, int h,
                    int i, int j, int n) {
    return locals(n + (a + b + c + d + e + f + g + h + i + j) * 0);
}

static int param_wrap(int a, int b, int c, int d, int e, int f, int g, int h,
                      int i, v8f32 w, v16f32 x) {
    return param(a + b + c + d + e + f + g + h + i, w, x);
}

int main(void) {
    int    r;
    v8f32  w = {1, 2, 3, 4, 5, 6, 7, 8};
    v16f32 x = {1};
    if ((r = locals(1)))
        return 10 + r;
    if ((r = stacky(0, 0, 0, 0, 0, 0, 0, 0, 0, 2)))
        return 20 + r;
    if ((r = stacky10(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3)))
        return 25 + r;
    if ((r = param(1, w, x)))
        return 30 + r;
    if ((r = param_wrap(0, 0, 0, 0, 0, 0, 0, 0, 0, w, x)))
        return 35 + r;
    return 42;
}
