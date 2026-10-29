// Macro arguments are cut from the token stream in place; a parameter used
// more than once, stringized, pasted or inside __VA_OPT__ must still see the
// whole argument, including nested macro calls.
#include <string.h>

#define ID(x)     x
#define TWICE(x)  ((x) + (x))
#define STR(x)    #x
#define XSTR(x)   STR(x)
#define CAT(a, b) a##b
#define NEST(x)   ID(ID(x))
#define BOTH(x)   (x) * XSTR(x)[0] + STR(x)[0]
#define SUM(...)  (0 __VA_OPT__(+ID(__VA_ARGS__)))
#define EMPTY()
#define ONE(x)     x
#define PICK(a, b) ID(a) - ID(b)

int main(void) {
    if (TWICE(ID(ID(3))) != 6)
        return 1;
    if (strcmp(XSTR(TWICE(ID(4))), "((4) + (4))") != 0)
        return 2;
    if (CAT(1, 2) != 12)
        return 3;
    if (NEST(ID(5)) != 5)
        return 4;
    if (BOTH(ID(2)) != 2 * '2' + '2')
        return 5;
    if (SUM() != 0 || SUM(ID(7)) != 7)
        return 6;
    if (ONE(EMPTY()) 8 != 8)
        return 7;
    if (PICK(ID(9), ID(4)) != 5)
        return 8;
    return 42;
}
