// A file-scope call can instantiate a template over a type the file declares
// when the file's own code does not name that instance.
typedef struct {
    int a;
    int b;
    int c;
    int d;
} Point;

@template(T) struct Box {
    T    value;
    char tag;
};

@comptime void gen(void) {
    Type *box = InstantiateType("Box", "Point");
    Obj  *fn  = MakeFunction("box_size", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(box))));
}
gen();

int main(void) {
    return box_size() + 22;
}
