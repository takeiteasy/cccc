// Regression for #1155: incidental whitespace immediately inside a
// captured #include's angle brackets/quotes ("#include < glob.h>", note the
// space) is tolerated by CCCC's own preprocessor -- join_tokens()/
// read_include_filename() skip the separator before the first filename
// token, so the resolved path is still exactly "glob.h" -- but was replayed
// verbatim into -c=native output, where a real host cc's preprocessor
// treats the whole `<...>`/`"..."` span as one opaque header-name token and
// fails to find a file literally named " glob.h". Covers both capture
// paths: the @emit route (copy_routed_directive_line) and @shared route
// (also copy_routed_directive_line, but through a different call site).
// Must compile and pass under both the VM run and --native.

#include @emit < stddef.h>
#include @shared < time.h>

[[cccc::comptime]]
int time_type_size(void) {
    return (int)sizeof(time_t) > 0;
}

[[cccc::comptime]]
void gen_answer(void) {
    Obj *fn = MakeFunction("include_spacing_answer", GetType("int"));
    FunctionSetBody(
        fn, MakeReturn(MakeIntLiteral(time_type_size() ? 42 : 1)));
}

gen_answer();

int include_spacing_answer(void);

int main(void) {
    size_t s = sizeof(int);
    (void)s;
    time_t t;
    (void)t;
    return include_spacing_answer();
}
