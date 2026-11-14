// A file-scope generator call sees the file's templates. The instances it
// makes are the ones the file's own code names.
@template(T) struct Box {
    T value;
};

@template(T) T twice(T a) {
    return a + a;
}

@template(T, int N) struct Arr {
    T v[N];
};

@comptime void gen(void) {
    Type *box = InstantiateType("Box", "int");
    InstantiateFunction("twice", "int");

    TemplateArgument a[]   = {TypeArg(GetType("char")),
                              ValueArg(MakeIntLiteral(6))};
    Type            *arr   = InstantiateTypeWith("Arr", a, 2);

    Obj             *sizes = MakeFunction("sizes", GetType("int"));
    FunctionSetBody(sizes,
                    MakeReturn(MakeIntLiteral(TypeSize(box) + TypeSize(arr))));

    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("twice_int(5)")));
}
gen();

int main(void) {
    Box @(int) b       = {3};
    Arr @(char, 6) arr = {{0}};
    return sizes() + ten() + twice @(int)(b.value) + b.value + sizeof arr + 7;
}
