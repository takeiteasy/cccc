// -c=native: a pointer global initialized from a member of a tagless-struct
// global
static struct {
    int a, b;
} g            = {40, 2};
static int *pa = &g.a;
int main(void) {
    return *pa + g.b;
}
