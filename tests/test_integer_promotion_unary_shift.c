// Unary +, ~ and the shifts promote a sub-int operand to int.
#include <stdbool.h>

int main(void) {
    char           c  = 1;
    unsigned char  uc = 0;
    short          s  = 1;
    bool           b  = true;
    enum { E }     e  = E;

    if (sizeof(+c) != sizeof(int) || sizeof(~c) != sizeof(int) ||
        sizeof(c << 1) != sizeof(int) || sizeof(c >> 1) != sizeof(int))
        return 1;
    if (sizeof(+s) != sizeof(int) || sizeof(~s) != sizeof(int) ||
        sizeof(s << 1) != sizeof(int))
        return 2;
    if (sizeof(+uc) != sizeof(int) || sizeof(~uc) != sizeof(int) ||
        sizeof(+b) != sizeof(int) || sizeof(~b) != sizeof(int))
        return 3;
    if (sizeof(+e) != sizeof(int) || sizeof(+1L) != sizeof(long) ||
        sizeof(~1LL) != sizeof(long long))
        return 4;

    if (_Generic(+c, int: 0, default: 1) || _Generic(~uc, int: 0, default: 1) ||
        _Generic(s << 1, int: 0, default: 1))
        return 5;

    if (~uc != -1 || (uc << 8) != 0 || (unsigned char)~uc != 255)
        return 6;
    c = 100;
    if ((c << 2) != 400 || (+c) != 100)
        return 7;
    c <<= 2;
    if (c != (char)400)
        return 8;
    c = ~c;
    if (c != (char)~(char)(400))
        return 9;
    return 42;
}
