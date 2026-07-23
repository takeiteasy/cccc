// EXPECT_COMPILE_ERROR
// Vector-by-value through a variadic '...' parameter is not supported
// (tracker #714): variadic args go through the integer/float register
// classification only (ENT3 spills them for va_arg to read), with no
// by-memory (scratch-slot pointer) path wired up. Must be rejected with a
// clear diagnostic rather than silently mis-marshalling.

typedef float v4sf __attribute__((vector_size(16)));

int my_variadic(const char *fmt, ...);

int main(void) {
    v4sf a;
    a[0] = 1.0f;
    return my_variadic("%d", a);
}

int my_variadic(const char *fmt, ...) {
    return 0;
}
