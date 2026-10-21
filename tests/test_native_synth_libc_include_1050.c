// a reflection-API comptime builder (Serialize()'s memcpy, or the
// Memcpy()/Strcmp() macros here) can resolve a call to memcpy/strlen/strcmp/etc
// with no #include of the declaring header ever reaching -c=native output --
// either a fresh Obj ensure_libc_fn_decl() (src/reflection.c) synthesizes with
// no token/file at all, or a genuine Obj reflection.h's own internal #include
// <string.h> parse leaves in scope (compile_macro_program()'s unconditional
// implicit_reflection_tokens() call, not gated on custom-attribute usage) --
// never a captured user #include either way, so auto-capture has nothing to
// replay. Both shapes reach 'call to undeclared library function' from the host
// compiler. Fixed by register_synth_libc_call() (reflection.c), reached
// centrally via var_ref_lookup(), recording {Obj, header} into
// vm->compiler.synth_libc_decls; serialize_synth_libc_includes() (serialize.c)
// emits the real header for whichever entries a program's emitted functions
// actually call, rather than a prototype that could conflict with the real
// declaration. Asserts VM 42 -> native 42
[[cccc::comptime]]
void generate_wrappers_1050(void) {
    Obj *cpy = MakeFunction("wrap_memcpy_1050", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "dst", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "src", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "n", GetType("long"));
    WithFn(cpy) {
        FunctionSetBody(cpy, MakeReturn(Memcpy(MakeParamRef(cpy, "dst"),
                                               MakeParamRef(cpy, "src"),
                                               MakeParamRef(cpy, "n"))));
    }

    Obj *cmp = MakeFunction("wrap_strcmp_1050", GetType("int"));
    FunctionAddParam(cmp, "a", MakePointer(GetType("char")));
    FunctionAddParam(cmp, "b", MakePointer(GetType("char")));
    WithFn(cmp) {
        FunctionSetBody(cmp, MakeReturn(Strcmp(MakeParamRef(cmp, "a"),
                                               MakeParamRef(cmp, "b"))));
    }
}

generate_wrappers_1050();

int main(void) {
    char src[6] = "hello";
    char dst[6] = {0};
    wrap_memcpy_1050(dst, src, 6);
    if (wrap_strcmp_1050(dst, "hello") != 0)
        return 1;
    return 42;
}
