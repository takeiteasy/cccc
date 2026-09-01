// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'gx'
//
// Ticket #1250: file-scope comptime generation runs before the runtime
// translation unit is parsed. Reading a same-named runtime global from the
// comptime body's own source (`int probe = gx;` below) makes the demand-
// driven declaration index splice a *copy* of `gx` into the comptime
// program itself, with storage in the comptime program's own data segment
// -- it is not, and cannot be, the runtime `gx`. A later Quote() naming
// `gx` used to silently resolve that comptime copy (via macro_context_scope
// chaining onto the runtime scope during comptime execution) and write it,
// leaving the real runtime `gx` untouched with no diagnostic at all. It is
// now a clear "undefined variable" error instead, exactly as if the probe
// line were never there -- see man/MACROS.md's comptime-vs-runtime global
// visibility section.

int gx = 1;

[[cccc::comptime]]
void gen(void) {
    int probe = gx;
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("{ gx = 99; return 7; }"));
    }
}
gen();

int main(void) {
    return gx == 99 ? 1 : 42;
}
