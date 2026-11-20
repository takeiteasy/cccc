// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: assignment to 'x' in an initializer
// CCCC_EXPECT_STDERR: assignment to 'i' in an initializer

struct Outer {
    struct {
        int x;
    } in;
    int y;
};

union U {
    int   i;
    float f;
};

int main(void) {
    int          x = 0, i = 0;
    struct Outer o = {x = 40};
    union U      u = {i = 2};
    return o.in.x + u.i;
}
