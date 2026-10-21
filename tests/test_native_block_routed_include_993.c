// A block's by-value capture of a struct declared in a cccc-only-routed include
// round-trips as real C.
#include "fixtures/routedlib_993.c"
int main(void) {
    struct Pt p;
    p.x            = 42;
    int (^b)(void) = ^{
      return p.x;
    };
    return b();
}
