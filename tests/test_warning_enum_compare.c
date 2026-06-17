// CCCC_FLAGS: -Wenum-compare
// CCCC_EXPECT_STDERR: comparison between values of different enum types 'Color' and 'Direction'.*\[-Wenum-compare\]

typedef enum Color { RED, GREEN, BLUE } Color;
typedef enum Direction { NORTH, SOUTH, EAST, WEST } Direction;

int main(void) {
    Color c = RED;
    Direction d = NORTH;
    (void)(c == d);
    return 42;
}
