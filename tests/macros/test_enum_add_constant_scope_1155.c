// Regression for #1155: VarScope (parse_internal.h) and VarScopeNode
// (cccc.h) had silently diverged in layout -- #1095 added
// enum_layout_ty/enum_layout_is_align to the end of VarScope but never to
// VarScopeNode, so every allocation (push_scope() casting a VarScopeNode*
// to VarScope*) aliased those two fields onto VarScopeNode's own
// name/name_len. Two or more EnumAddConstant calls on the same enum,
// referencing a constant OTHER than the last one added, is the minimal
// shape that exposes it: EnumAddConstant("RED", ...) writes name/name_len
// through the VarScopeNode view; the immediately following
// EnumAddConstant("GREEN", ...) call's own scope-push then aliases new
// data over what "RED"'s node thinks is its enum_layout_ty/
// enum_layout_is_align (harmless, since nothing read them under the VM)
// -- but read back through the VarScope view (primary(), parse_postfix.c),
// GREEN's raw scope-push wrote RED's *name pointer* as a bogus Type*,
// which the -c=native serializer then dereferenced -> SIGSEGV. A single
// EnumAddConstant call, or referencing only the LAST constant added, does
// not reproduce (no second push_scope() runs after the one whose fields
// get read back). Must pass both under the plain VM run and under
// --native.

[[cccc::comptime]]
Node *make_enum(void) {
    Type *e = MakeEnum("Signal1155");
    EnumAddConstant(e, "SIG_RED", 0);
    EnumAddConstant(e, "SIG_GREEN", 1);
    EnumAddConstant(e, "SIG_BLUE", 2);
    if (EnumCount(e) != 3)
        MacroErrorAt(0, "MakeEnum: wrong constant count");
    return MakeIntLiteral(0);
}
make_enum();

int main(void) {
    // Reference a non-last constant -- the shape that segfaulted.
    if (SIG_RED != 0)
        return 1;
    if (SIG_GREEN != 1)
        return 2;
    if (SIG_BLUE != 2)
        return 3;
    return 42;
}
