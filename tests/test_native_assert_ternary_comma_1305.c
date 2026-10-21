// assert() with a ternary or comma operand round-trips under -c=native.
#include <assert.h>
static int checked_add(int a, int b) {
    assert(a >= 0);
    assert(b >= 0);
    int s = a + b;
    assert(s == a + b);
    return s;
}
int main(void) {
    int c = 0, e = 0;
    int r = (c = 1, c + 1) ? (c + 10) : (e = 99, e + 1);
    if (r != 11 || e != 0)
        return 1;
    return checked_add(20, 22) - 20 + 20;
}
