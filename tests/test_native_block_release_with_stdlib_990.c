// Block_copy/Block_release round-trip when <stdlib.h> is in scope: the extern
// free() must not collide with the real one.
#include <stdlib.h>
typedef int (^IntBlock)(void);
IntBlock make_adder(int x) {
    IntBlock inner = ^{
      return x + 1;
    };
    return Block_copy(inner);
}
int main(void) {
    IntBlock a = make_adder(41);
    int      r = a();
    Block_release(a);
    return r == 42 ? 42 : 1;
}
