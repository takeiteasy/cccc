// CCCC_FLAGS: --std=c23
struct Point {
    int x;
    int y;
};

struct Point {
    int x;
    int y;
};

union Value {
    int i;
    long l;
};

union Value {
    long l;
    int i;
};

enum Color : unsigned int {
    RED = 1,
    GREEN = 2,
};

enum Color : unsigned int {
    GREEN = 2,
    RED = 1,
};

int main(void) {
    struct Point p = { 20, 22 };
    union Value v;
    enum Color c = GREEN;
    v.l = 42;
    if (p.x + p.y != 42) return 1;
    if (v.i != 42) return 2;
    if (c != 2) return 3;
    if (RED != 1 || GREEN != 2) return 4;
    return 42;
}
