// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: return shared_base_1258 \+ shared_bump_1258;
//
// #1258: the #1249/#1250 comptime-global splice guard must not hide a
// declaration-only extern that `#include @shared` put in scope. The guard
// exists to stop a Quote() template silently writing the comptime program's
// own *defined* shadow copy of a same-named runtime global (see
// test_macros_quote_global_after_probe.c, the negative case); an @shared
// extern has no storage of its own to shadow, and naming it from a Quote()
// template is the whole point of @shared.
//
// Before the fix this failed with a hard "undefined variable
// 'shared_base_1258'" at generation time. This is a shape assertion on the
// serialized output; the execution-level round trip for the same pattern
// (every generated body is Quote() text referencing `ccccl_nil` / `ccccl_t`
// from an @shared header) is examples/ccccl, exercised by its own `make
// check` / `make native`.
#include @shared "quote_shared_extern_global_1258.h"

int shared_base_1258 = 40;
int shared_bump_1258 = 2;

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn,
                        Quote("{ return shared_base_1258 + shared_bump_1258; }"));
    }
}
gen();

int main(void) {
    return f();
}
