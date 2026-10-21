// issignaling round-trips under -c=native.
#include <math.h>

int main(void) {
    float  sf = 0.0f / 0.0f;
    double sd = 0.0 / 0.0;
    int    a  = issignaling(sf);
    int    b  = issignaling(sd);
    int    c  = iseqsig(1.0f, 1.0f);
    int    d  = iseqsig(1.0, 1.0);
    if (a != 0 && a != 1)
        return 1;
    if (b != 0 && b != 1)
        return 2;
    if (!c)
        return 3;
    if (!d)
        return 4;
    return 42;
}
