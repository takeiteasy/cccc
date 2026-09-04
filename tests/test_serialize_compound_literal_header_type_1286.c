// Ticket #1286: a file-scope pointer initialized from a compound literal
// (`T *x = &(T){...};`) whose type T is declared in an *included* header,
// not the primary file, lost its anonymous backing global entirely under
// -c=native -- the compound literal's synthesized storage
// (rename_anon_globals(), src/serialize_program.c) inherits obj->tok from
// the *type*'s name/tag token (new_var(), src/parse_core.c), and
// global_is_header_supplied() misread that as "this global is already
// supplied by the header's own replayed #include", suppressing both its
// forward declaration and its definition while the reference to it
// (serialize_reloc_init(), src/serialize_decl.c) was still emitted --
// producing a real host compiler's "use of undeclared identifier
// '__cccc_TypeName_N'". Reproduced directly against src/type.c's own
// scalar-type singleton pattern (`Type *ty_decimal64 = &(Type){...};`).
//
// Two shapes in one file, both header-declared: a tagged struct and an
// untagged one (the untagged case additionally exercises
// type_needs_anon_aggregate()'s own forward-declaration path, checked
// ahead of the fix's gate). This test carries no special CCCC_FLAGS --
// the VM path never touched src/serialize*.c and so could never have
// caught this; the ambient -c=native round-trip suite (#1157, on by
// default) is what actually exercises the fix.
#include "fixtures/compound_literal_header_type_1286.h"

TaggedPoint1286   *tagged_1286   = &(TaggedPoint1286){.x = 10, .y = 20};
UntaggedThing1286 *untagged_1286 = &(UntaggedThing1286){.kind = 1, .size = 12};

int main(void) {
    if (tagged_1286->x + tagged_1286->y != 30)
        return 1;
    if (untagged_1286->kind + untagged_1286->size != 13)
        return 2;
    return 42;
}
