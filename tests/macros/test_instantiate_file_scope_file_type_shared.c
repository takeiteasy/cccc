// A file-scope call's instance over a type the file declares is the instance
// the file's own code names: one Type, so values pass between them.
typedef struct {
    int a;
    int b;
} Point;

struct Tagged {
    int x;
};

typedef struct {
    Point at;
    int   z;
} Nested;

struct Node {
    int          v;
    struct Node *next;
};

enum Color { RED, GREEN = 5 };

@template(T) struct Box {
    T value;
};

@comptime void gen(void) {
    InstantiateType("Box", "Point");
    InstantiateType("Box", "struct Tagged");
    InstantiateType("Box", "Nested");
    InstantiateType("Box", "struct Node");
    InstantiateType("Box", "enum Color");
}
gen();

static int sum(Point *p) {
    return p->a + p->b;
}

int main(void) {
    Box @(Point) bp         = {{10, 20}};
    Point q                 = bp.value;
    Box @(struct Tagged) bt = {{4}};
    struct Tagged t         = bt.value;
    Box @(Nested) bn        = {{{1, 2}, 3}};
    Nested n                = bn.value;
    Point  inner            = bn.value.at;
    Box @(struct Node) bl   = {{7, 0}};
    struct Node l           = bl.value;
    Box @(enum Color) bc    = {GREEN};
    enum Color c            = bc.value;
    if (sum(&bp.value) != 30 || q.b != 20 || t.x != 4 || n.z != 3 ||
        inner.b != 2 || l.v != 7 || c != 5)
        return 1;
    return 42;
}
