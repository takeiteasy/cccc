// CCCC_FLAGS: -Wall
// CCCC_REJECT_STDERR: excess-init
//
// False-positive canary: none of these are excess, so -Wexcess-init (part
// of -Wall) must stay silent for all of them.

struct S {
    int a;
    int b;
};

struct Flex {
    int n;
    int data[];
};

int main(void) {
    int         x[]  = {1, 2, 3}; // flexible-size array, exact fit
    char        a[4] = "abcd";    // exact fit, NUL dropped (legal C)
    char        c[]  = "abcd";    // flexible-size string
    int         y[3] = {1, 2, 3}; // exact-fit array
    struct S    s    = {1, 2};    // exact-fit struct
    struct S    e    = {};        // C23 empty initializer
    struct Flex f    = {1};       // flexible array member, no excess

    (void)f;
    return x[2] + (a[0] - 'a') + (c[3] - 'd') + y[2] + s.b + e.a + 34;
}
