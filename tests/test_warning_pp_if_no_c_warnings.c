// CCCC_FLAGS: -Wall -Wextra -Werror
//
// C semantic warnings (-Wlogical-op, -Wtautological-compare) do not fire on
// preprocessor conditionals; -Werror turns any stray one into a compile error.
#include <stddef.h>

#define A 1
#define B 2
#define X 3

#if defined(A) && defined(B)
#define BOTH 1
#endif

#if X >= X
#define SELF_CMP 1
#elif X == X
#define SELF_CMP 2
#endif

#if __STDC_VERSION__ >= 202311L || 1
#define STD_OK 1
#endif

#if !BOTH || !SELF_CMP || !STD_OK
#error "preprocessor conditionals evaluated wrongly"
#endif

int main(void) {
    return 42;
}
