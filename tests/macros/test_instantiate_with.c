// InstantiateTypeWith() and InstantiateFunctionWith() instantiate a template
// from Type and constant handles, so a computed type needs no spelling.
typedef struct {
    int a;
    int b;
} Point;

typedef struct {
    char c[3];
} Anon;

@template(T) struct Box {
    T value;
};

@template(T, int N) struct Arr {
    T v[N];
};

@template(T) T twice(T a) {
    return a + a;
}

@template(int... Ns) struct Dims {
    int n[sizeof...(Ns)];
};

@template(Ts...) struct Count {
    char n[sizeof...(Ts)];
};

@comptime Node *anon_size(void) {
    TemplateArgument a[] = {TypeArg(GetType("Anon"))};
    return MakeIntLiteral(TypeSize(InstantiateTypeWith("Box", a, 1)));
}

@comptime Node *arr_size(void) {
    TemplateArgument a[] = {TypeArg(GetType("char")),
                            ValueArg(MakeIntLiteral(5))};
    return MakeIntLiteral(TypeSize(InstantiateTypeWith("Arr", a, 2)));
}

@comptime Node *dims_size(void) {
    TemplateArgument a[] = {ValueArg(MakeIntLiteral(2)),
                            ValueArg(MakeIntLiteral(3)),
                            ValueArg(MakeIntLiteral(4))};
    return MakeIntLiteral(TypeSize(InstantiateTypeWith("Dims", a, 3)));
}

@comptime Node *count_size(void) {
    TemplateArgument a[] = {TypeArg(GetType("int")), TypeArg(GetType("char")),
                            TypeArg(GetType("Anon")), TypeArg(GetType("long"))};
    return MakeIntLiteral(TypeSize(InstantiateTypeWith("Count", a, 4)));
}

@comptime Node *doubled(Node *x) {
    TemplateArgument a[] = {TypeArg(GetType("int"))};
    Obj             *fn  = InstantiateFunctionWith("twice", a, 1);
    return Quote("twice__int($1)", x);
}

int main(void) {
    Box @(Point) p = {{1, 2}};
    return anon_size() + arr_size() + dims_size() + count_size() + doubled(5) +
           p.value.b + 6;
}
