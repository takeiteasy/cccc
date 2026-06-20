// Ticket #335: custom file-scope attributes backed by comptime macros.

@macro(attribute("serialize"))
void define_serializer(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "serialize expected a type target");
    if (!AttrTargetName(target))
        MacroErrorAt(0, "serialize target has no name");

    Type *ty = $ATTR_TARGET_TYPE(target);
    Obj *fn = MakeFunction("serialize_Point", GetType("int"));
    FunctionAddParam(fn, "p", MakePointer(ty));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return sizeof(struct Point);"));
    }
    PublishNode(fn);
}

@serialize
struct Point {
    int x;
    int y;
};

@macro(attribute("answer"))
void define_answer(AttrTarget *target, Node *value) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_GLOBAL)
        MacroErrorAt(0, "answer expected a global target");

    Obj *fn = MakeFunction("custom_attr_answer", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(value));
    }
    PublishNode(fn);
}

@answer(123)
int configured_value;

@macro(attribute("typedef_size"))
void define_typedef_size(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPEDEF)
        MacroErrorAt(0, "typedef_size expected a typedef target");

    Obj *fn = MakeFunction("custom_attr_typedef_size", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return sizeof(AliasPoint);"));
    }
    PublishNode(fn);
}

@typedef_size
typedef struct Point AliasPoint;

int main(void) {
    struct Point p = {1, 2};
    if (serialize_Point(&p) != (int)sizeof(struct Point))
        return 1;
    if (custom_attr_answer() != 123)
        return 2;
    if (custom_attr_typedef_size() != (int)sizeof(AliasPoint))
        return 3;
    return 42;
}
