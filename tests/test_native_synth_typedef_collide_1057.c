// A program with its own size_t typedef is not given a synthesized <stddef.h>.
typedef unsigned long size_t;

[[cccc::comptime]]
void generate_size_1057_collide(void) {
    Obj *fn = MakeFunction("get_size_1057_collide", GetType("size_t"));
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
    }
}

generate_size_1057_collide();

int main(void) {
    return (int)get_size_1057_collide();
}
