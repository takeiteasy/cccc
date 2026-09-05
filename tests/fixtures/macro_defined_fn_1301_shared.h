// Fixture for tests/test_serialize_macro_defined_fn_1301.c (#1301, negative
// guard). A captured header whose macro *produces* a function definition
// when invoked -- it does not itself write any function text; the
// definition only exists wherever V1301_DEFINE_THING(T) is actually
// invoked. This is the shape function_is_header_supplied() must NOT
// suppress: the resulting declarator name's spelling location (this
// header, where the macro is defined) must not be confused with its
// expansion location (the invoking file, which is what really needs to
// supply the body).
#ifndef MACRO_DEFINED_FN_1301_SHARED_H
#define MACRO_DEFINED_FN_1301_SHARED_H

#define V1301_DEFINE_THING(T) \
    int thing_##T(void) { return 21; }

#endif // MACRO_DEFINED_FN_1301_SHARED_H
