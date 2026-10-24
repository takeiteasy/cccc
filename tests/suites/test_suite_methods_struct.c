// CCCC_FLAGS: --testing
// Suite: a function written in a struct body is a method named T_f.
#pragma cccc suite begin "methods_struct"

typedef struct Vec2 {
    int x, y;
    int len(Vec2 *self) {
        return self->x + self->y;
    }
    int twice(Vec2 *self) {
        return self->later() * 0 + self->len() * 2;
    }
    int later(Vec2 *self);
    int sum(const Vec2 *self, int k) {
        return self->x + self->y + k;
    }
    int first(Vec2 v) {
        return v.x;
    }
    int width(Vec2 *self) {
        return sizeof(Vec2);
    }
    static int stat(Vec2 *self) {
        return self->y;
    }
    inline int inl(Vec2 *self) {
        return self->x * 10;
    }
    int (*cb)(int);
    char tag[4] __attribute__((aligned(4)));
} Vec2;

int Vec2_later(Vec2 *self) {
    return 1;
}

typedef struct {
    int a;
    int get(Anon *self) {
        return self->a;
    }
} Anon;

typedef struct Node {
    int v;
    int val(Node *n) {
        return n->v;
    }
} Node;

struct Tagged {
    int v;
    int val(struct Tagged *t) {
        return t->v;
    }
};

union Pun {
    int   i;
    float f;
    int geti(union Pun *u) {
        return u->i;
    }
};

typedef struct Inner {
    int n;
    int id(struct Inner *s) {
        return s->n + 100;
    }
} Alias;

static int double_it(int a) {
    return a * 2;
}

[[cccc::test(return = 42)]]
int test_struct_method_basic(void) {
    Vec2  v = {3, 4};
    Vec2 *p = &v;
    if (v.len() != 7)
        return 1;
    if (p->len() != 7)
        return 2;
    if (v.sum(5) != 12)
        return 3;
    if (v.first() != 3)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_calls_later_method(void) {
    Vec2 v = {3, 4};
    if (v.twice() != 14)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_is_plain_function(void) {
    Vec2 v = {3, 4};
    if (Vec2_len(&v) != 7)
        return 1;
    int (*fp)(Vec2 *) = &Vec2_len;
    if (fp(&v) != 7)
        return 2;
    if (Vec2_later(&v) != 1)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_no_layout_change(void) {
    if (sizeof(Vec2) != sizeof(struct {
            int x, y;
            int (*cb)(int);
            char tag[4] __attribute__((aligned(4)));
        }))
        return 1;
    Vec2 v = {1, 2};
    if (v.width() != sizeof(Vec2))
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_static_and_inline(void) {
    Vec2 v = {3, 4};
    if (v.stat() != 4)
        return 1;
    if (v.inl() != 30)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_member_function_pointer_still_a_member(void) {
    Vec2 v = {1, 2, double_it};
    if (v.cb(4) != 8)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_anonymous_typedef(void) {
    Anon a = {7};
    if (a.get() != 7)
        return 1;
    if (Anon_get(&a) != 7)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_tag_and_typedef_names(void) {
    Node          n  = {5};
    struct Tagged t  = {6};
    Alias         al = {7};
    if (n.val() != 5 || Node_val(&n) != 5)
        return 1;
    if (t.val() != 6 || Tagged_val(&t) != 6)
        return 2;
    if (al.id() != 107 || Alias_id(&al) != 107)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_method_union(void) {
    union Pun u = {9};
    if (u.geti() != 9 || Pun_geti(&u) != 9)
        return 1;
    return 42;
}

#pragma cccc suite end
