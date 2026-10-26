// CCCC_FLAGS: -Wchecked-calls
// CCCC_REJECT_STDERR: checked-calls
// Checked regions: calls -Wchecked-calls does not report -- an annotated
// prototype, a header declaration, a call in an [[cccc::unchecked]] block, a
// call outside any region, and a call through a function pointer.

#include <string.h>

int annotated(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[0] + p[1] + n - 2;
}

int raw(int *p) {
    return p[0];
}

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(2)]] a = (int _Checked[2]){20, 22};
    int (*fp)(int *)                       = raw;
    int total                              = annotated(a, 2);
    total += (int)strlen("") + (int)strlen("ab") - 2;
    [[cccc::unchecked]] { total += raw(&total) - total; }
    (void)fp;
    return total;
}

int main(void) {
    return f();
}
