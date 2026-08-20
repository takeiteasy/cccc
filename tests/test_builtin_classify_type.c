// __builtin_classify_type (GCC extension, added for tracker #721 as the
// <stdarg.h> va_arg discriminant for a by-pointer variadic vector argument).
// Follows gcc's typeclass.h codes where a matching CCCC type exists
// (integer=1, char=2, enumeral=3, boolean=4, pointer=5, real=8, complex=9,
// function=10, record=12, union=13, array=14); vectors have no gcc
// counterpart and get a CCCC-specific code, 99 (CCCC_VECTOR_TYPE_CLASS in
// src/parse.c). Only the exact vector code is load-bearing for CCCC itself
// (consumed by va_arg); the rest are for gcc-compatible source to compile.

typedef float v4sf __attribute__((vector_size(16)));

enum color { RED, GREEN, BLUE };
struct point {
    int x, y;
};
union u {
    int   i;
    float f;
};

int main(void) {
    if (__builtin_classify_type(1) != 1)
        return 1; // int
    char ch = 'a';
    if (__builtin_classify_type(ch) != 2)
        return 2; // char
    // Note: a char *constant* like 'a' has type int in C, not char --
    // classify_type follows the operand's actual type, so a variable is
    // needed here to exercise the char case.
    if (__builtin_classify_type((enum color)RED) != 3)
        return 3;  // enum
    if (__builtin_classify_type((_Bool)1) != 4)
        return 4;  // bool
    if (__builtin_classify_type((void *)0) != 5)
        return 5;  // pointer
    if (__builtin_classify_type(1.0) != 8)
        return 6;  // double
    if (__builtin_classify_type(1.0f) != 8)
        return 7;  // float
    struct point p;
    if (__builtin_classify_type(p) != 12)
        return 8;  // struct
    union u un;
    if (__builtin_classify_type(un) != 13)
        return 9;  // union
    int arr[4];
    if (__builtin_classify_type(arr) != 14)
        return 10; // array

    v4sf v;
    if (__builtin_classify_type(v) != 99)
        return 11; // vector

    // Non-evaluation: the operand's *type* is used at compile time, exactly
    // like sizeof(expr) -- the expression itself must never be emitted or
    // evaluated. Two checks:
    //   1. Dereferencing a null pointer of vector type (as va_arg does via
    //      __builtin_classify_type(*(type *)0)) must not fault under -3
    //      (pointer checks) -- if the parser mistakenly gen_expr'd the
    //      operand, this would trap at runtime.
    //   2. An operand with a visible side effect must not execute it.
    if (__builtin_classify_type(*(v4sf *)0) != 99)
        return 12;
    if (__builtin_classify_type(*(int *)0) != 1)
        return 13;

    int x   = 0;
    int cls = __builtin_classify_type(x++);
    if (cls != 1)
        return 14;
    if (x != 0)
        return 15; // x++ must not have run

    return 42;
}
