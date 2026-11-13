// A file-scope generator call selects a partial or full specialization, and
// the file's own code names the same instance afterwards.
@template(T) struct Cell {
    T    v;
    char tag;
};

@template(T) @specialize(T *) struct Cell {
    T   *v;
    char tag[10];
};

@specialize(char) struct Cell {
    char v;
};

@comptime void gen(void) {
    Type *plain   = InstantiateType("Cell", "int");
    Type *pointer = InstantiateType("Cell", "int *");
    Type *full    = InstantiateType("Cell", "char");
    Obj  *fn      = MakeFunction("shape", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(plain) * 1000 +
                                                  TypeSize(pointer) * 10 +
                                                  TypeSize(full))));
}
gen();

int main(void) {
    Cell @(char) c  = {1};
    Cell @(int *) p = {0};
    return shape() == 8000 + 240 + 1 ? 42 : 1;
}
