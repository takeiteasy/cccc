// A static of a function-local type (tagless member) round-trips under
// -c=native.
int main(void) {
    struct S {
        int v;
    };
    static struct {
        struct S s;
        int      n;
    } w = {{40}, 2};
    return w.s.v + w.n;
}
