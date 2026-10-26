// The initializer checks are part of --checked-pointers: without it the same
// overrunning initializers compile and run.

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int      buf[2];
struct S g                             = {8, buf};
int *[[cccc::array, cccc::count(3)]] v = buf;

int main(void) {
    return 42;
}
