// -c=native, transitive/nested block capture and an escaping Block_copy
// round-trip as real C
#include <stdlib.h>
typedef int (^IntBlock)(void);
IntBlock make_adder(int x) {
    IntBlock inner = ^{
      return x + 1;
    };
    return Block_copy(inner);
}
int main(void) {
    IntBlock a  = make_adder(41);
    int      ra = a();
    Block_release(a);
    int outer           = 10;
    int (^level1)(void) = ^{
      int mid             = outer + 1;
      int (^level2)(void) = ^{
        return mid + outer;
      };
      return level2();
    };
    int r2 = level1();
    return (ra == 42 && r2 == 21) ? 42 : 1;
}
