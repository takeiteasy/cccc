// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT:  ,  ,
// CCCC_REJECT_STDOUT:  , ;
// -m output has no dangling comma in the initializer.
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
