// A by-value member whose struct was first reached through a pointer is defined
// before its user under -c=native.

typedef int (*EarlyFn)(struct Early *);

struct Early {
    int  tag;
    long value;
};

struct Holder {
    char         prefix[8];
    struct Early e;
};

static int use_early(struct Early *e) {
    return e->tag;
}

int main(void) {
    EarlyFn fn = use_early;
    (void)fn;
    struct Holder h;
    h.e.tag   = 7;
    h.e.value = 123456789L;
    if (h.e.tag != 7)
        return 1;
    if (h.e.value != 123456789L)
        return 2;
    return 42;
}
