// CCCC_FLAGS: --testing
// Suite: operators accept the operand types C allows.
#include <complex.h>

#pragma cccc suite begin "operand_types"

typedef enum { E_ONE = 1, E_TWO = 2 } E;
typedef int v4 __attribute__((vector_size(16)));

[[cccc::test]] void test_logical_not_takes_pointer(void) {
    int  x = 0;
    int *p = &x;
    int *n = nullptr;
    AssertEq(!p, 0);
    AssertEq(!n, 1);
}

[[cccc::test]] void test_arithmetic_operators_take_floating_operands(void) {
    double d = 2.5;
    int    x = 4;
    AssertEq((int)(x * d), 10);
    AssertEq((int)(d / 0.5), 5);
    AssertEq((int)-d, -2);
    AssertEq((int)+d, 2);
}

[[cccc::test]] void test_integer_operators_take_bool_and_enum(void) {
    bool b = true;
    E    e = E_TWO;
    AssertEq(b << 1, 2);
    AssertEq(+b, 1);
    AssertEq(~e & 0xff, 0xfd);
    AssertEq(e | E_ONE, 3);
    AssertEq(e % 2, 0);
}

[[cccc::test]] void test_complex_arithmetic(void) {
    double _Complex z = 1.0 + 2.0 * I;
    z                 = -z * 2;
    AssertEq((int)creal(z), -2);
    AssertEq((int)cimag(z), -4);
}

[[cccc::test]] void test_integer_vector_bitwise(void) {
    v4 a = {1, 2, 3, 4};
    v4 b = {1, 1, 1, 1};
    v4 c = a & b;
    AssertEq(c[0], 1);
    AssertEq(c[1], 0);
    v4 d = ~a;
    AssertEq(d[0], ~1);
}

#pragma cccc suite end
