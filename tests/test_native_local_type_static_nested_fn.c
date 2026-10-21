// A static of a function-local type (nested fn) round-trips under -c=native.
int main(void) {
    struct S {
        int v;
    };
    static struct S a = {40};
    int g(void) {
        return a.v + 2;
    }
    return g();
}
