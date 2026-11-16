// CCCC_FLAGS: --testing
// Complex values as call arguments and results, the C99 complex math
// functions, and the complex arms of <tgmath.h>.

#include <complex.h>
#include <stdarg.h>
#include <tgmath.h>

typedef double _Complex dc;
typedef float _Complex fc;
typedef long double _Complex lc;

static int near(double a, double b) {
    return fabs(a - b) < 1e-6;
}

static dc add(dc a, dc b) {
    return a + b;
}
static dc mul(dc a, dc b) {
    return a * b;
}
static fc add_float(fc a, fc b) {
    return a + b;
}
static lc add_long(lc a, lc b) {
    return a + b;
}
static dc mix(int i, dc a, double d, dc b, fc c) {
    return a * i + b * d + c;
}
static dc many(dc a, dc b, dc c, dc d, dc e, dc f, dc g, dc h, dc i, dc j) {
    return a + b + c + d + e + f + g + h + i + j;
}
static dc doubling(int n, dc acc) {
    return n == 0 ? acc : doubling(n - 1, acc * CMPLX(2, 0));
}
static dc modify(dc z) {
    z = z + 1;
    return z;
}

struct Holder {
    dc  z;
    int k;
};
static struct Holder make_holder(dc z) {
    struct Holder h = {z, 7};
    return h;
}

static dc va_sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    dc s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, dc);
    va_end(ap);
    return s;
}

[[cccc::test(return = 42)]]
int test_call_and_return(void) {
    dc a = CMPLX(1, 2), b = CMPLX(3, 4);
    dc r = add(a, b);
    if (creal(r) != 4 || cimag(r) != 6)
        return 1;
    r = add(mul(a, b), add(a, b)); // calls as arguments
    if (creal(r) != -1 || cimag(r) != 16)
        return 2;
    add(a, b);                     // discarded result
    if (creal(add(a, b) + add(a, b)) != 8)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_float_widths(void) {
    fc fr = add_float(CMPLXF(1, 2), CMPLXF(0.5f, 0.5f));
    if (crealf(fr) != 1.5f || cimagf(fr) != 2.5f)
        return 1;
    lc lr = add_long(CMPLXL(1, 2), CMPLXL(3, 4));
    if (creall(lr) != 4 || cimagl(lr) != 6)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_function_pointer(void) {
    dc (*fp)(dc, dc) = mul;
    dc r             = fp(CMPLX(1, 2), CMPLX(3, 4));
    return creal(r) == -5 && cimag(r) == 10 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_block_call(void) {
    dc (^blk)(dc, dc) = ^dc(dc a, dc b) {
      return a * b;
    };
    dc r = blk(CMPLX(1, 2), CMPLX(3, 4));
    if (creal(r) != -5 || cimag(r) != 10)
        return 1;
    blk(CMPLX(1, 2), CMPLX(3, 4)); // discarded result
    return 42;
}

static dc block_return(dc (^blk)(dc), dc z) {
    return blk(z);
}

[[cccc::test(return = 42)]]
int test_block_call_returned(void) {
    dc r = block_return(
        ^dc(dc z) {
          return z + 1;
        },
        CMPLX(1, 2));
    return creal(r) == 2 && cimag(r) == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_block_mixed_arguments(void) {
    dc offset = CMPLX(10, 20);
    dc (^blk)(int, dc, double, fc, long double) =
        ^dc(int i, dc a, double d, fc c, long double l) {
          return a * i + d + c + l + offset;
        };
    dc r = blk(2, CMPLX(1, 2), 0.5, CMPLXF(1, 1), 1.5L);
    return creal(r) == 15 && cimag(r) == 25 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_block_expression_arguments(void) {
    dc (^blk)(int, dc, double) = ^dc(int i, dc a, double d) {
      return a * i + d;
    };
    dc one = CMPLX(1, 1);
    dc r   = blk(3, one + CMPLX(0, 1), 2.0);
    return creal(r) == 5 && cimag(r) == 6 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_block_call_in_arguments(void) {
    dc (^blk)(int, dc, double) = ^dc(int i, dc a, double d) {
      return a * i + d;
    };
    dc r = blk(3, add(CMPLX(1, 1), CMPLX(0, 1)), 2.0);
    return creal(r) == 5 && cimag(r) == 6 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_block_float_widths(void) {
    fc (^fb)(fc) = ^fc(fc z) {
      return z + z;
    };
    lc (^lb)(lc) = ^lc(lc z) {
      return z * z;
    };
    fc f = fb(CMPLXF(1, 2));
    lc l = lb(CMPLXL(1, 2));
    return crealf(f) == 2 && cimagf(f) == 4 && creall(l) == -3 && cimagl(l) == 4
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_mixed_and_stack_arguments(void) {
    dc r = mix(2, CMPLX(1, 2), 0.5, CMPLX(3, 4), CMPLXF(1, 1));
    if (creal(r) != 4.5 || cimag(r) != 7)
        return 1;
    dc a = CMPLX(1, 2);
    r    = many(a, a, a, a, a, a, a, a, a, a);
    return creal(r) == 10 && cimag(r) == 20 ? 42 : 2;
}

[[cccc::test(return = 42)]]
int test_recursion(void) {
    dc r = doubling(5, CMPLX(1, 1));
    return creal(r) == 32 && cimag(r) == 32 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_argument_is_a_copy(void) {
    dc a = CMPLX(1, 2);
    dc c = modify(a);
    return creal(a) == 1 && creal(c) == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_real_argument_converts(void) {
    return creal(add(CMPLX(1, 2), 1.0)) == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_member_through_call(void) {
    struct Holder h = make_holder(CMPLX(1, 2));
    return creal(h.z) == 1 && cimag(h.z) == 2 && h.k == 7 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_variadic_complex(void) {
    dc r = va_sum(3, CMPLX(1, 2), CMPLX(3, 4), CMPLX(1, 2));
    return creal(r) == 5 && cimag(r) == 8 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_conditional_expression(void) {
    int n = 1;
    dc  r = n ? CMPLX(1, 2) : CMPLX(3, 4);
    dc  s = !n ? CMPLX(1, 2) : CMPLX(3, 4);
    return creal(r) == 1 && creal(s) == 3 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_math_functions(void) {
    dc z = CMPLX(1.0, 2.0);
    dc e = cexp(z);
    if (!near(creal(e), exp(1.0) * cos(2.0)) ||
        !near(cimag(e), exp(1.0) * sin(2.0)))
        return 1;
    dc s = csqrt(CMPLX(-4.0, 0.0));
    if (!near(creal(s), 0) || !near(cimag(s), 2))
        return 2;
    dc q = clog(cexp(CMPLX(0.5, 0.25)));
    if (!near(creal(q), 0.5) || !near(cimag(q), 0.25))
        return 3;
    dc p = cpow(CMPLX(0, 1), CMPLX(2, 0));
    if (!near(creal(p), -1))
        return 4;
    dc t = csin(CMPLX(0, 0));
    if (!near(creal(t), 0) || !near(creal(ccos(CMPLX(0, 0))), 1))
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_math_function_widths(void) {
    fc sf = csqrtf(CMPLXF(-9.0f, 0.0f));
    if (!near(crealf(sf), 0) || !near(cimagf(sf), 3))
        return 1;
    lc sl = csqrtl(CMPLXL(-16.0, 0.0));
    if (!near(creall(sl), 0) || !near(cimagl(sl), 4))
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_real_valued_functions(void) {
    if (!near((cabs)(CMPLX(3, 4)), 5) || !near(cabsf(CMPLXF(3, 4)), 5))
        return 1;
    if (!near(carg(CMPLX(0, 1)), 1.5707963267948966))
        return 2;
    double (*fp)(double _Complex) = cabs;
    if (!near(fp(CMPLX(3, 4)), 5))
        return 3;
    dc (*cp)(dc) = cexp;
    if (!near(creal(cp(CMPLX(0, 0))), 1))
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_tgmath_complex_arms(void) {
    dc e = exp(CMPLX(1.0, 1.0));
    if (!near(creal(e), 1.4686939399158851) ||
        !near(cimag(e), 2.2873552871788423))
        return 1;
    if (!near(sqrt(16.0), 4.0) || !near(sqrt(16), 4.0))
        return 2;
    fc f = sqrt(CMPLXF(-4.0f, 0));
    if (!near(cimagf(f), 2.0))
        return 3;
    lc l = sqrt(CMPLXL(-9.0, 0));
    if (!near(cimagl(l), 3.0))
        return 4;
    dc p = pow(CMPLX(0, 1), 2);
    if (!near(creal(p), -1) || !near(pow(2, 10), 1024))
        return 5;
    if (!near(cosh(0.0), 1.0) || !near(creal(cosh(CMPLX(0, 0))), 1.0))
        return 6;
    if (!near(fabs(CMPLX(3, 4)), 5) || !near(fabs(-2.5), 2.5))
        return 7;
    return 42;
}
