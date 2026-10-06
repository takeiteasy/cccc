// CCCC_FLAGS: --dangling-pointers
// A live array reached through a pointer (alias, pointer walk, &buf[i] passed
// to a callee, pointer held in a struct) is not reported as dangling when a
// dead frame's escaping scalar tag sits inside it: forming the pointer makes
// the array escape, so its extent is tagged live.
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

struct W {
    char *p;
};

static void put(char *p, char v) {
    *p = v;
}
static int sum(const char *p) {
    int s = 0;
    for (int i = 0; i < 256; i++)
        s += p[i];
    return s;
}

static int alias(void) {
    char  buf[256];
    char *q = buf;
    for (int i = 0; i < 256; i++)
        q[i] = (char)i;
    return sum(q);
}

static int walk(void) {
    char  buf[256];
    char *q = buf;
    for (int i = 0; i < 256; i++)
        *q++ = (char)i;
    return sum(buf);
}

static int callee(void) {
    char buf[256];
    for (int i = 0; i < 256; i++)
        put(&buf[i], (char)i);
    return sum(buf);
}

static int held(void) {
    char     buf[256];
    struct W w;
    w.p = buf;
    for (int i = 0; i < 256; i++)
        w.p[i] = (char)i;
    return sum(w.p);
}

int main(void) {
    f(1);
    if (alias() != -128)
        return 1;
    f(1);
    if (walk() != -128)
        return 2;
    f(1);
    if (callee() != -128)
        return 3;
    f(1);
    if (held() != -128)
        return 4;
    return 42;
}
