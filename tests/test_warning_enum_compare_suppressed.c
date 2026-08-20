// CCCC_FLAGS: -Wenum-compare -Wno-enum-compare
// CCCC_EXPECT_STDOUT: done

#include <stdio.h>

typedef enum Color { RED, GREEN, BLUE } Color;
typedef enum Direction { NORTH, SOUTH, EAST, WEST } Direction;

int main(void) {
    Color     c = RED;
    Direction d = NORTH;
    (void)(c == d);
    printf("done\n");
    return 42;
}
