// A static of a function-local type (tagged) round-trips under -c=native.
int main(void) {
    static struct S {
        int   loc;
        void *fn;
    } buf[4];
    buf[0].loc = 40;
    buf[1].loc = 2;
    return buf[0].loc + buf[1].loc;
}
