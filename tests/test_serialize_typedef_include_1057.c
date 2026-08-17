// Ticket #1057: a comptime builder that folds a standard scalar typedef
// name -- GetType("size_t")/"ptrdiff_t"/"wchar_t" -- into a generated
// function's signature has no #include reaching -c=native output for it,
// split off from #1050's own investigation (that ticket covered the *call*
// half -- memcpy/strlen/strcmp resolving with no #include of their own;
// this is the *type-name* half, a different mechanism).
//
// Root cause: GetType("size_t") misses __builtin_ast_get_type()'s small
// builtins table (src/reflection.c), falls through to find_type_in_scope,
// and on a miss triggers cc_comptime_resolve_type_name() (src/macros.c),
// which demand-splices and re-parses `typedef unsigned long size_t;` out of
// CCCC's own bundled include/stddef.h. record_type_name() (src/parse_core.c)
// therefore marks it from_include=true, and typedef_alias_header_
// suppressed() (src/serialize.c) drops its alias line under -c=native/-m on
// the assumption the user's own #include supplies it -- but nothing here
// ever does, so the host compiler sees a bare, undeclared "size_t".
//
// Fixed by serialize_synth_typedef_includes() (src/serialize.c), the type-
// name sibling of #1050's serialize_synth_libc_includes(): a small
// {name, header} table (size_t/ptrdiff_t/wchar_t -> <stddef.h>, the trio
// verified to match the real host's own typedef on every supported combo)
// plus a usage walk (obj_needs_synth_typedef_header, mirroring collect_
// obj_types()'s traversal shape) that emits the real #include on demand,
// never a printed typedef -- a printed one would collide with any other
// path that transitively reaches the real <stddef.h> in the same TU.
//
// This file exercises all three names directly through GetType(), each in
// its own generated function's return type, and checks the resulting
// values round-trip -- not just that the program compiles. Deliberately no
// #include <stddef.h> anywhere in this file: the whole point is that
// nothing in the TU ever names these types itself, so the only path that
// can bring them into the -c=native output is the fix under test. main()
// below compares the return values against plain integer literals rather
// than spelling size_t/ptrdiff_t/wchar_t itself, so this file's own source
// never requires them to be declared.

[[cccc::comptime]]
void generate_typedef_wrappers_1057(void) {
    Obj *sz = MakeFunction("get_size_1057", GetType("size_t"));
    WithFn(sz) {
        FunctionSetBody(sz, MakeReturn(MakeIntLiteral(1057)));
    }

    Obj *pd = MakeFunction("get_ptrdiff_1057", GetType("ptrdiff_t"));
    WithFn(pd) {
        FunctionSetBody(pd, MakeReturn(MakeIntLiteral(-7)));
    }

    Obj *wc = MakeFunction("get_wchar_1057", GetType("wchar_t"));
    WithFn(wc) {
        FunctionSetBody(wc, MakeReturn(MakeIntLiteral(97)));
    }
}

generate_typedef_wrappers_1057();

int main(void) {
    if (get_size_1057() != 1057)
        return 1;
    if (get_ptrdiff_1057() != -7)
        return 2;
    if (get_wchar_1057() != 97)
        return 3;

    return 42;
}
