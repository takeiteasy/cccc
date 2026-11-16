// A preprocessing number runs through underscores, so `10_15` is one token
// that pastes into a name and stringizes whole.
#include <string.h>

#define __MAC_10_15 101500
#define CAT(m)      __MAC_##m
#define STR(m)      #m
#define AT_LEAST(m) (defined(HOST) && HOST >= __MAC_##m)
#define HOST        140000

#if !AT_LEAST(10_15)
#error "defined() inside a pasted macro lost the 10_15 token"
#endif

#define DECL(m) int var_##m = 7
DECL(10_15);

int main(void) {
    if (CAT(10_15) != 101500 || var_10_15 != 7)
        return 1;
    if (strcmp(STR(10_15), "10_15") != 0)
        return 2;
    return 42;
}
