// Ticket #235: @ctor custom attribute.

#include <cccc/derive.h>

@ctor struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point p = Point_new(3, 4);
    if (p.x != 3)
        return 1;
    if (p.y != 4)
        return 2;
    return 42;
}
