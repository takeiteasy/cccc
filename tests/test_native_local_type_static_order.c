// A static of a function-local type (order) round-trips under -c=native.
int main(void) {
    struct S {
        int v;
    };
    static struct S  a = {40};
    static struct S *p = &a;
    static struct S  b = {2};
    return p->v + b.v;
}
