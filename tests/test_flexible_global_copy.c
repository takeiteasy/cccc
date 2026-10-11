// Expected return: 42
// Copying an initialised flexible-array struct copies sizeof(declared struct)
// bytes through assignment, return, comma and conditional; the bytes after the
// struct stay untouched.

struct F {
    int       n;
    long long x;
    int       tail[];
};

struct F gf = {2, 5, {10, 20, 30}};

struct F get(void) {
    return gf;
}

static int intact(long long *buf) {
    for (int i = 2; i < 8; i++)
        if (buf[i] != -1)
            return 0;
    return 1;
}

static void fill(long long *buf) {
    for (int i = 0; i < 8; i++)
        buf[i] = -1;
}

int main(void) {
    long long buf[8];
    struct F *p = (struct F *)buf;

    fill(buf);
    *p = gf;
    if (p->n != 2 || p->x != 5 || !intact(buf))
        return 1;

    fill(buf);
    *p = get();
    if (p->n != 2 || p->x != 5 || !intact(buf))
        return 2;

    fill(buf);
    *p = (0, gf);
    if (p->n != 2 || p->x != 5 || !intact(buf))
        return 3;

    int pick = buf[0] != 0;
    fill(buf);
    *p = pick ? gf : gf;
    if (p->n != 2 || p->x != 5 || !intact(buf))
        return 4;

    static struct F sf = {7, 9, {1, 2}};
    fill(buf);
    *p = sf;
    if (p->n != 7 || p->x != 9 || !intact(buf))
        return 5;
    return 42;
}
