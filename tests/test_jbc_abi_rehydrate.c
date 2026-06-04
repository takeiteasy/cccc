// Regression coverage for .jbc ABI rehydration.
// Expected return: 42

#include "stdarg.h"

struct Pair {
    int a;
    int b;
};

union Word {
    int i;
};

struct Pair make_pair(int a, int b) {
    struct Pair p;
    p.a = a;
    p.b = b;
    return p;
}

int pair_sum(struct Pair p) {
    return p.a + p.b;
}

union Word make_word(int value) {
    union Word w;
    w.i = value;
    return w;
}

struct Pair sum_var_pairs(int count, ...) {
    va_list ap;
    va_start(ap, count);

    struct Pair p;
    p.a = 0;
    p.b = 0;
    for (int i = 0; i < count; i++) {
        p.a += va_arg(ap, int);
        p.b += va_arg(ap, int);
    }

    va_end(ap);
    return p;
}

int *static_array(void) {
    static int values[3];
    values[0] = 10;
    values[1] = 20;
    values[2] = 12;
    return values;
}

int globals[2];

int *global_array(void) {
    return globals;
}

int main(void) {
    struct Pair p = make_pair(10, 32);
    if (pair_sum(p) != 42)
        return 1;

    union Word w = make_word(42);
    if (w.i != 42)
        return 2;

    struct Pair vp = sum_var_pairs(2, 5, 7, 11, 19);
    if (vp.a != 16)
        return 3;
    if (vp.b != 26)
        return 4;

    int *s = static_array();
    if (s[0] + s[1] + s[2] != 42)
        return 5;

    int *g = global_array();
    g[0] = 21;
    g[1] = 21;
    if (globals[0] + globals[1] != 42)
        return 6;

    return 42;
}
