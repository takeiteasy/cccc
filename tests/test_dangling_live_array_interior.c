// CCCC_FLAGS: --dangling-pointers
// A live frame indexing its own non-escaping array must not read as dangling
// when a dead frame's escaping scalar tag lands inside the array.
static int sink(void *p) {
    return p != 0;
}
static int f(int n) {
    int  a = n;
    char junk[100];
    junk[0] = (char)n;
    sink(&a);
    return a + junk[0];
}
static int g(void) {
    char buf[256];
    int  s = 0;
    for (int i = 0; i < 256; i++)
        buf[i] = (char)i;
    for (int i = 0; i < 256; i++)
        s += buf[i];
    return s;
}
int main(void) {
    f(1);
    return g() == -128 ? 42 : 1;
}
