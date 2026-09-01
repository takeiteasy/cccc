// Fixture for tests/test_serialize_generated_secondary_1262.c (#1262).
//
// The PRIMARY input (input_files[0]). It carries the comptime emitter and
// one plain #include of its own; that #include MUST survive into
// -c=generated output. The test file itself is input_files[1] -- the
// non-primary input -- and its directives must NOT be replayed.
#include <wctype.h>

[[cccc::comptime]] void gen_1262(void) {
    Obj *fn = MakeFunction("answer_1262", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
    PublishNode(fn);
}

gen_1262();

int answer_1262(void);

int main(void) {
    return answer_1262();
}
