// A block's by-value capture of a header-declared type (struct tm) round-trips
// as real C.
#include <time.h>
int main(void) {
    struct tm t;
    t.tm_year      = 42;
    int (^b)(void) = ^{
      return t.tm_year;
    };
    return b();
}
