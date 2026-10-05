// The cleanup attribute on a variable-length array runs at scope exit and
// receives a pointer to the array's own storage.

static int  calls;
static int *seen;
static void rel(int (*p)[]) {
    seen = *p;
    calls++;
}
static void rel_any(void *p) {
    seen = p;
    calls++;
}

static int order[4];
static int norder;
static void rec_int(int *p) {
    order[norder++] = *p;
}
static void rec_vla(void *p) {
    (void)p;
    order[norder++] = 100;
}

static void *seen_target;
static void rel_ptr(void *p) {
    seen_target = *(void **)p;
    calls++;
}

static int basic(int k) {
    int *addr;
    calls = 0;
    {
        int v[k] __attribute__((cleanup(rel)));
        v[0] = 1;
        addr = v;
    }
    return calls == 1 && seen == addr;
}

static int two_dimensional(int n, int m) {
    int *addr;
    calls = 0;
    {
        int v[n][m] __attribute__((cleanup(rel_any)));
        v[0][0] = 1;
        addr    = &v[0][0];
    }
    return calls == 1 && seen == addr;
}

static int lifo(int k) {
    norder = 0;
    {
        int a __attribute__((cleanup(rec_int))) = 1;
        int v[k] __attribute__((cleanup(rec_vla)));
        int b __attribute__((cleanup(rec_int))) = 2;
        v[0]                                    = 0;
    }
    return norder == 3 && order[0] == 2 && order[1] == 100 && order[2] == 1;
}

static int return_exit(int k, int c) {
    int v[k] __attribute__((cleanup(rel)));
    v[0] = 0;
    if (c)
        return 7;
    return 8;
}

static int jump_exits(int k) {
    calls = 0;
    for (;;) {
        int v[k] __attribute__((cleanup(rel)));
        v[0] = 0;
        break;
    }
    if (calls != 1)
        return 0;
    {
        int v[k] __attribute__((cleanup(rel)));
        v[0] = 0;
        goto out;
    }
out:
    return calls == 2;
}

// A jump before the declaration leaves nothing to clean up.
static int jump_before_decl(int k, int c) {
    calls = 0;
    for (;;) {
        if (c)
            break;
        int v[k] __attribute__((cleanup(rel)));
        v[0] = 0;
        break;
    }
    return calls == (c ? 0 : 1);
}

static int per_iteration(int k) {
    calls = 0;
    for (int i = 0; i < 3; i++) {
        int v[k] __attribute__((cleanup(rel)));
        v[0] = i;
    }
    return calls == 3;
}

static int pointer_to_vla(int k) {
    int a[3];
    calls       = 0;
    seen_target = 0;
    {
        int (*p)[k] __attribute__((cleanup(rel_ptr))) = &a;
        (void)p;
    }
    if (calls != 1 || seen_target != (void *)a)
        return 0;
    calls       = 0;
    seen_target = 0;
    {
        int (*p)[k] __attribute__((cleanup(rel_ptr)));
        p = &a;
        (void)p;
    }
    return calls == 1 && seen_target == (void *)a;
}

int main(void) {
    if (!basic(3))
        return 1;
    if (!two_dimensional(2, 3))
        return 2;
    if (!lifo(3))
        return 3;
    calls = 0;
    if (return_exit(3, 1) != 7 || calls != 1)
        return 4;
    if (return_exit(3, 0) != 8 || calls != 2)
        return 5;
    if (!jump_exits(3))
        return 6;
    if (!jump_before_decl(3, 1) || !jump_before_decl(3, 0))
        return 7;
    if (!per_iteration(3))
        return 8;
    if (!pointer_to_vla(3))
        return 9;
    return 42;
}
