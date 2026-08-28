// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'undefined_thing_in_attr_handler'
// CCCC_EXPECT_STDERR: note: while handling attribute '@serialize' on 'struct Point'
// Ticket #966: a compile error inside a declaration an @attr handler
// publishes must carry a "note: while handling attribute ..." line naming
// the attribute and the decl it was attached to -- the reported location
// (inside the handler's generated function body) is several frames removed
// from the @serialize the user actually wrote on struct Point.

@comptime(attribute("serialize")) void define_serializer(AttrTarget *target) {
    Obj *fn = MakeFunction("serialize_Point", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("undefined_thing_in_attr_handler"));
    }
    PublishNode(fn);
}

@serialize struct Point {
    int x;
    int y;
};

int main(void) {
    return 0;
}
