// An empty initializer of a host-owned type round-trips under -c=native.
#include <time.h>
struct Wrap1289 {
    struct timespec ts;
};
int main(void) {
    struct timespec ts = {};
    struct Wrap1289 w;
    w.ts = (struct timespec){};
    (void)ts;
    (void)w;
    return 42;
}
