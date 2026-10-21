// A pointer to an anonymous struct assigned from realloc round-trips under
// -c=native.
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
    int r = (int)g.patches[0].loc + g.patches[0].fn;
    free(g.patches);
    return r;
}
