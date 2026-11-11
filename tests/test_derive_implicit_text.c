// @from_string on a struct loads <cccc/text.h> without an #include.
#include <stdio.h>

@to_string @from_string typedef struct {
    int x;
    int y;
} Point;

int main(void) {
    Point p = {3, 4}, q;
    char  buf[64];
    Point_to_string(&p, buf, sizeof buf);
    return Point_from_string(&q, buf) == 0 && q.x == 3 && q.y == 4 ? 42 : 1;
}
