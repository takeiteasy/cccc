// Fixture for tests/test_serialize_macro_declared_global_1303.c (#1303,
// global_is_header_supplied() counterpart to #1301's function fix). A
// captured header whose macro *defines* a global when invoked -- it does
// not itself write any global text; the definition only exists wherever
// V1303_DEFINE_G(n) is actually invoked. The resulting name's spelling
// location (this header, where the macro is defined) must not be confused
// with its expansion location (the invoking file, which is what really
// needs to supply the definition).
#ifndef MACRO_DECLARED_GLOBAL_1303_SHARED_H
#define MACRO_DECLARED_GLOBAL_1303_SHARED_H

#define V1303_DEFINE_G(n) int g1303_##n = 21;

#endif // MACRO_DECLARED_GLOBAL_1303_SHARED_H
