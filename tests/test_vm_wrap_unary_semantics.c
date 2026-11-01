#include <limits.h>
// The unary and divide operations whose wrap the peephole drops or fuses must
// still give the value their type denotes. Values arrive through noinline
// parameters so nothing is folded.
typedef long long ll;

[[gnu::noinline]] int neg_s(int x) {
    return -x;
}
[[gnu::noinline]] unsigned neg_z(unsigned x) {
    return -x;
}
[[gnu::noinline]] ll neg_wide(int x) {
    return -(ll)x;
}
[[gnu::noinline]] ll lnot(int x) {
    return !x;
}
[[gnu::noinline]] ll bnot(int x) {
    return ~x;
}
[[gnu::noinline]] unsigned bnot_z(unsigned x) {
    return ~x;
}
[[gnu::noinline]] int div_c(int x) {
    return x / 2;
}
[[gnu::noinline]] int div_1(int x) {
    return x / 1;
}
[[gnu::noinline]] int div_v(int x, int y) {
    return x / y;
}

int main(void) {
    if (neg_s(INT_MIN) != INT_MIN || neg_s(5) != -5 || neg_s(-5) != 5 ||
        neg_s(INT_MAX) != -INT_MAX)
        return 1;
    if (neg_z(1) != UINT_MAX || neg_z(0) != 0 || neg_z(UINT_MAX) != 1)
        return 2;
    if (neg_wide(INT_MIN) != -(ll)INT_MIN)
        return 3;
    if (lnot(0) != 1 || lnot(7) != 0 || lnot(INT_MIN) != 0)
        return 4;
    if (bnot(0) != -1 || bnot(INT_MIN) != INT_MAX || bnot_z(0) != UINT_MAX)
        return 5;
    if (div_c(INT_MIN) != INT_MIN / 2 || div_c(-7) != -3 || div_c(7) != 3 ||
        div_1(INT_MIN) != INT_MIN)
        return 6;
    if (div_v(INT_MIN, 2) != INT_MIN / 2 || div_v(-9, 3) != -3)
        return 7;
    return 42;
}
