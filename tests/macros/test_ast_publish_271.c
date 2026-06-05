// Test ticket #271: unified _AST_PUBLISH API.

[[jcc::macro]]
void publish_proto(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *proto = _AST_FUNCTION_PROTOTYPE("published_add_one", int_ty);
    _AST_FUNCTION_ADD_PARAM(proto, "x", int_ty);
    _AST_PUBLISH(proto);
}
publish_proto();

[[jcc::macro]]
void define_published_fn(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("published_add_one", int_ty);
    _AST_FUNCTION_SET_BODY(fn,
        _AST_RETURN(_AST_BINARY(_ADD, _AST_PARAM_REF(fn, "x"),
                                _AST_INT_LITERAL(1))));
}
define_published_fn();

[[jcc::macro]]
void publish_global_and_types(void) {
    _Type *char_ty = _AST_GET_TYPE("char");
    _Type *arr_ty = _AST_MAKE_ARRAY(char_ty, 4);
    _Obj *g = _AST_GLOBAL_VAR("published_bytes", arr_ty);
    _AST_GLOBAL_VAR_SET_INIT_DATA(g, "JCC\0", 4);
    _AST_PUBLISH_AT(g, _AST_SYNTHETIC_TOKEN("published global"));

    _Type *int_ty = _AST_GET_TYPE("int");
    _Type *point = _AST_MAKE_STRUCT("PublishedPoint");
    _AST_STRUCT_ADD_FIELD(point, "x", int_ty);
    _AST_STRUCT_ADD_FIELD(point, "y", int_ty);
    _AST_PUBLISH(point);

    _Type *tagged = _AST_MAKE_ENUM("PublishedTag");
    _AST_ENUM_ADD_CONSTANT(tagged, "PUBLISHED_TAG_OK", 7);
    _AST_PUBLISH(tagged);

    _AST_PUBLISH(_AST_MAKE_TYPEDEF("PublishedLong", _AST_GET_TYPE("long")));
}
publish_global_and_types();

[[jcc::macro]]
void forward_alias_still_works(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *proto = _AST_FUNCTION_PROTOTYPE("published_alias_fn", int_ty);
    _AST_FORWARD_DECLARE(proto);
}
forward_alias_still_works();

[[jcc::macro]]
void define_alias_fn(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("published_alias_fn", int_ty);
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(5)));
}
define_alias_fn();

int main(void) {
    if (published_add_one(41) != 42) return 1;
    if (published_bytes[0] != 'J') return 2;
    if (published_bytes[1] != 'C') return 3;
    if (published_bytes[2] != 'C') return 4;

    struct PublishedPoint p;
    p.x = 20;
    p.y = 22;
    if (p.x + p.y != 42) return 5;

    PublishedLong n = 42;
    if (n != 42) return 6;
    if (PUBLISHED_TAG_OK != 7) return 7;
    if (published_alias_fn() != 5) return 8;
    return 42;
}
