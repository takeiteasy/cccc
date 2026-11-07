# A comptime-generated function whose parameter is a complete `struct S` may be
# declared in another input file where `struct S` is opaque: each input file is
# its own translation unit with its own Type for the tag.
. "$(dirname "$0")/lib.sh"

cat >a.c <<'SRC'
struct S { int v; };
struct S the_s = {42};
[[cccc::comptime]]
Node *make_get(void) {
    Type *ptr = MakePointer(GetType("S"));
    Obj  *fn  = MakeFunction("get", GetType("int"));
    FunctionAddParam(fn, "p", ptr);
    WithFn(fn) {
        FunctionSetBody(fn, Quote("{ return $1->v; }", MakeParamRef(fn, "p")));
    }
    return MakeIntLiteral(0);
}
make_get();
SRC
printf 'struct S;\nextern int get(struct S *);\nextern struct S the_s;\nint main(void) { return get(&the_s); }\n' >b.c

expect_exit 42 "$CCCC" a.c b.c
