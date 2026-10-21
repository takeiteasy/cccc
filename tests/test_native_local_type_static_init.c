// A static of a function-local type (init) round-trips under -c=native.
int main(void) {
    static struct S {
        int loc;
        int fn;
    } buf[2]    = {{40, 1}, {2, 3}};
    struct S *p = buf;
    return p[0].loc + p[1].loc;
}
