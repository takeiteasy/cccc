// A file-scope aggregate is a valid template argument in a file that also has
// a file-scope generator call.
typedef struct {
    int a;
    int b;
} Point;

@template(T) struct Box {
    T value;
};

@comptime void gen(void) {
    Obj *fn = MakeFunction("seven", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(7)));
}
gen();

int main(void) {
    Box @(Point) p = {{1, 2}};
    return seven() + p.value.b + 33;
}
