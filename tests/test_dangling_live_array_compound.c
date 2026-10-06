// CCCC_FLAGS: --dangling-pointers
// Compound assignment, ++/-- and member RMW on a live frame's own arrays go
// through to_assign()'s `*tmp` desugar; they must not read as dangling when a
// dead frame's escaping scalar tag sits inside the array.
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

struct item {
    int f;
    int g;
};
struct holder {
    int         arr[64];
    struct item items[8];
};

static int g(void) {
    char          buf[256];
    int           m[8][8];
    struct holder h;
    for (int i = 0; i < 256; i++)
        buf[i] = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            m[i][j] = 0;
    for (int i = 0; i < 64; i++)
        h.arr[i] = 10;
    for (int i = 0; i < 8; i++) {
        h.items[i].f = 1;
        h.items[i].g = 2;
    }

    for (int i = 0; i < 256; i++) {
        buf[i] += 1;
        buf[i]++;
        ++buf[i];
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            ++m[i][j];
    for (int i = 0; i < 64; i++)
        h.arr[i] -= 1;
    for (int i = 0; i < 8; i++) {
        h.items[i].f *= 3;
        h.items[i].g++;
    }

    int s = 0;
    for (int i = 0; i < 256; i++)
        s += buf[i];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            s += m[i][j];
    for (int i = 0; i < 64; i++)
        s += h.arr[i];
    for (int i = 0; i < 8; i++)
        s += h.items[i].f + h.items[i].g;
    return s;
}

int main(void) {
    f(1);
    // 256*3 + 64 + 64*9 + 8*(3+3) = 768 + 64 + 576 + 48
    return g() == 1456 ? 42 : 1;
}
