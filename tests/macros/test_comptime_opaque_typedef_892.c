// Ticket #892: opaque-handle typedefs (`typedef struct Foo Foo;`) in a
// @shared header must not collide with cccc's internally synthesized
// opaque tags (e.g. reflection.h's AttrTarget) once a [[cccc::comptime]]
// function is present in the same file. This exercises the VM path end to
// end; the serializer defect itself (-m/-c=native output emitting the
// wrong struct tag) is covered by tools/comptime_native_smoke.py, since
// the serializer never runs for a plain VM-executed program like this one.

#include @shared "comptime_opaque_typedef_892.h"

[[cccc::comptime]]
Node *check_892(void) { return MakeIntLiteral(0); }

int main(void) {
    Widget *w = make_widget();
    Gadget *g = make_gadget();
    return widget_tag(w) + gadget_tag(g) + check_892() + 39;
}
