// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: g\.patches =[^\n]*\(struct \{
// CCCC_HOST_CC: -Werror=incompatible-pointer-types
// -m output does not cast to an anonymous struct and the host compiler accepts
// it.
#include <stdlib.h>
struct AnonPtr1300 {
    struct {
        long loc;
        int  fn;
    }  *patches;
    int n, cap;
};
static struct AnonPtr1300 g;
int main(void) {
    g.cap     = 4;
    g.patches = realloc(g.patches, (size_t)g.cap * sizeof(*g.patches));
    if (!g.patches)
        return 1;
    g.patches[0].loc = 40;
    g.patches[0].fn  = 2;
    return (int)g.patches[0].loc + g.patches[0].fn;
}
