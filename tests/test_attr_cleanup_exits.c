// __attribute__((cleanup(fn))) runs on every way out of a scope, in LIFO
// order, only for variables whose declaration was reached, matching gcc:
// natural block end, return (after the value is computed), goto, break,
// continue, switch, a for-init declaration, and a statement expression (whose
// value is copied before the cleanup runs).
#include <string.h>

#define CL __attribute__((cleanup(rel)))

static int log_[64], n;
static void rel(int *p) {
    log_[n++] = *p;
}

static int ret_uses(void) {
    int a CL = 7;
    a        = 8;
    return a * 10;
}

static void early_break(int x) {
    for (int i = 0; i < 1; i++) {
        if (x)
            break;
        int b CL = 99;
        (void)b;
    }
}

static void loop(void) {
    for (int i = 0; i < 3; i++) {
        int c CL = 100 + i;
        (void)c;
    }
}

static void go(void) {
    {
        int d CL = 200;
        {
            int e CL = 201;
            (void)e;
            goto out;
        }
        (void)d;
    }
out:;
}

static void nested_ret(void) {
    int f CL = 300;
    (void)f;
    {
        int g CL = 301;
        (void)g;
        return;
    }
}

static void ret_before_decl(void) {
    int a CL = 310;
    {
        (void)a;
        return;
    }
    int b CL = 311;
    (void)b;
}

static void sw(int k) {
    switch (k) {
        case 1: {
            int h CL = 400;
            (void)h;
            break;
        }
        default:
            break;
    }
}

static void cont(void) {
    for (int i = 0; i < 2; i++) {
        int j CL = 500 + i;
        if (i == 0)
            continue;
        (void)j;
    }
}

static void back_goto(void) {
    int k = 2;
    {
    again:;
        int b CL = 510 + k;
        if (k--)
            goto again;
        (void)b;
    }
}

static void for_init(void) {
    for (int i CL = 520; i < 522; i++) {
    }
}

static int stmt_expr(void) {
    int x = 5;
    return x + ({
               int s CL = 600;
               s + 1;
           });
}

struct P {
    int v;
};
static void relp(struct P *p) {
    log_[n++] = p->v;
    p->v      = -1;
}
static int stmt_expr_struct(void) {
    struct P r = ({
        struct P q __attribute__((cleanup(relp))) = {610};
        q;
    });
    return r.v;
}

int main(void) {
    static const int want[] = {8,   100, 101, 102, 201, 200, 301, 300, 310,
                               400, 500, 501, 512, 511, 510, 522, 600, 610};
    if (ret_uses() != 80)
        return 1;
    early_break(1);
    loop();
    go();
    nested_ret();
    ret_before_decl();
    sw(1);
    cont();
    back_goto();
    for_init();
    if (stmt_expr() != 606 || stmt_expr_struct() != 610)
        return 2;
    if (n != (int)(sizeof(want) / sizeof(want[0])) ||
        memcmp(log_, want, sizeof(want)) != 0)
        return 3;
    return 42;
}
