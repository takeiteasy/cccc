// Ticket #1050: a reflection-API comptime builder (Serialize()'s memcpy, or
// the Memcpy()/Strlen()/Strcmp() macros used directly here) can resolve a
// call to memcpy/strlen/strcmp/etc with no #include of the declaring header
// ever reaching -c=native output. Two distinct, independently-discovered
// shapes:
//
//   (1) ensure_libc_fn_decl() (src/reflection.c) synthesizes a fresh Obj
//       with no token/file at all, when nothing else has already declared
//       the name -- this is the path __builtin_ensure_string_h_decls()
//       takes, but that's only reached from ensure_reflection_attrs_
//       registered(), itself only triggered by custom-attribute usage
//       (@serialize/@deserialize et al).
//   (2) reflection.h's own internal `#include <string.h>` (added so
//       Memcpy()/Strlen()/Strcmp() "just work" without the TU writing its
//       own #include) gets parsed for real by compile_macro_program()'s
//       unconditional implicit_reflection_tokens() call -- reached for
//       *any* comptime program, no custom attribute required -- leaving a
//       genuine Obj in scope whose token names CCCC's own bundled include/
//       string.h. Never a captured user #include either way.
//
// Both shapes reach the host compiler as a bare, undeclared call ("call to
// undeclared library function 'memcpy'") under -c=native, even though the
// VM path works fine (the FFI registration for these functions is
// unconditional). This file exercises shape (2) directly: no custom
// attribute anywhere, just plain [[cccc::comptime]] functions calling
// Memcpy()/Strcmp() -- test_custom_attributes_serialize_235.c (via
// @serialize) covers shape (1).
//
// Fixed by register_synth_libc_call() (src/reflection.c), reached
// centrally from var_ref_lookup() (both the scope-hit and globals-hit
// branches, so it covers both shapes above uniformly) -- records
// {Obj, header} into vm->compiler.synth_libc_decls the moment a call
// resolves to one of the five known names. serialize_synth_libc_includes()
// (src/serialize.c) then emits `#include <string.h>` once, only if some
// emitted function's body actually calls one of the registered Objs
// (node_calls_obj(), the same identity-match helper the Block_copy/free
// shim check already uses) -- deliberately not a printed prototype, since
// the synthesized signatures are looser than the real ones and a printed
// prototype could conflict with <string.h>'s own declaration if it's also
// reached some other way in the same TU.

[[cccc::comptime]]
void generate_stdlib_wrappers_1050(void) {
    Obj *cpy = MakeFunction("wrap_memcpy_1050", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "dst", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "src", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "n", GetType("long"));
    WithFn(cpy) {
        FunctionSetBody(cpy, MakeReturn(Memcpy(MakeParamRef(cpy, "dst"),
                                               MakeParamRef(cpy, "src"),
                                               MakeParamRef(cpy, "n"))));
    }

    Obj *len = MakeFunction("wrap_strlen_1050", GetType("long"));
    FunctionAddParam(len, "s", MakePointer(GetType("char")));
    WithFn(len) {
        FunctionSetBody(len, MakeReturn(Strlen(MakeParamRef(len, "s"))));
    }

    Obj *cmp = MakeFunction("wrap_strcmp_1050", GetType("int"));
    FunctionAddParam(cmp, "a", MakePointer(GetType("char")));
    FunctionAddParam(cmp, "b", MakePointer(GetType("char")));
    WithFn(cmp) {
        FunctionSetBody(cmp, MakeReturn(Strcmp(MakeParamRef(cmp, "a"),
                                               MakeParamRef(cmp, "b"))));
    }
}

generate_stdlib_wrappers_1050();

int main(void) {
    char src[6] = "hello";
    char dst[6] = {0};

    wrap_memcpy_1050(dst, src, 6);
    if (wrap_strcmp_1050(dst, "hello") != 0)
        return 1;
    if ((int)wrap_strlen_1050(dst) != 5)
        return 2;

    return 42;
}
