// @hash and @equals need no header of the program's own: size_t and _Bool
// are not spelled by name, also under C17.
// CCCC_FLAGS: --std=c17

#include <cccc/derive.h>

@equals @hash struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point a = {1, 2};
    struct Point b = {1, 2};
    return Point_equals(&a, &b) && Point_hash(&a) == Point_hash(&b) ? 42 : 1;
}
