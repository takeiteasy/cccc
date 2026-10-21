// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: #include <stddef\.h>
// -m output does not force #include <stddef.h> on top of the program's own
// size_t typedef.
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
