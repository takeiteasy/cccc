// -c=native: a C11 anonymous member stays inline next to a same-shaped tagless
// global
struct T {
    int a;
    struct {
        int x;
    };
    union {
        int  u;
        char c;
    };
};
static struct {
    int x;
} g;
int main(void) {
    struct T t = {1};
    t.x        = 40;
    t.u        = 0;
    g.x        = 1;
    return t.a + t.x + g.x;
}
