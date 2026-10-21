// A static of a function-local type (tagless) round-trips under -c=native.
int main(void) {
    static struct {
        int   loc;
        void *fn;
    } buf[4];
    buf[0].loc = 42;
    return buf[0].loc;
}
