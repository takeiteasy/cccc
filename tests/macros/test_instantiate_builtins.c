// InstantiateType() and InstantiateFunction() instantiate a template from
// argument text.
typedef struct {
    int a;
    int b;
} Point;

@template(T) T maxof(T a, T b) {
    return a > b ? a : b;
}

@template(T, int N) struct Arr {
    T v[N];
};

@template(T) struct Box {
    T value;
};

@comptime Node *biggest(Node *a, Node *b) {
    Obj *fn = InstantiateFunction("maxof", "int");
    return Quote("maxof_int($1, $2)", a, b);
}

@comptime Node *bytes(void) {
    Type *box = InstantiateType("Box", "Point");
    Type *arr = InstantiateType("Arr", "char, 5");
    return MakeIntLiteral(TypeSize(box) + TypeSize(arr));
}

int main(void) {
    Box @(Point) p = {{1, 2}};
    return biggest(7, 12) + bytes() + p.value.b + 15;
}
