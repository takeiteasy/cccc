// CCCC_REJECT_STDERR: Wparentheses
// Parenthesized sub-expressions keep their meaning under -c=native and draw no
// -Wparentheses warning.
#include <stdbool.h>
int main(void) {
    int  a = 1, b = 2, c = 0, id = 0;
    bool ok = true, flag = false;
    ok    &= id != 0;
    flag  |= id == 0;
    int r  = a & (b != 0);
    int s  = (a + b) << 1;
    int t  = a || (b && c);
    int v  = a | (b & c);
    return (!ok && flag && r == 1 && s == 6 && t == 1 && v == 1) ? 42 : 1;
}
