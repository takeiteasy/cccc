// CCCC_FLAGS: -Wall -Werror=tautological-compare
// CCCC_REJECT_STDERR: self-comparison
//
// Constants that fold to the same value are not a self-comparison: gcc stays
// silent for sizeof, enumerators and folded casts.

#include <assert.h>

enum { E = 3 };
struct P {
    char c;
    int  i;
};

static_assert(sizeof(struct P) == 8);
static_assert(sizeof(int) == sizeof(int));
static_assert(E == E);

int main(void) {
    int r  = 0;
    r     += sizeof(struct P) == 8;
    r     += (long)8 == (long)8;
    r     += 1 == 1;
    return r == 3 ? 42 : 1;
}
