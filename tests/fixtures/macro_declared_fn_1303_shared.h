// Fixture for tests/test_serialize_macro_declared_fn_1303.c (#1303,
// bodyless_decl_from_input_or_bundled() counterpart to #1301's function
// fix). A captured header whose macro only *declares* (never defines) a
// function when invoked -- its declarator name is produced by a `##` paste
// of a macro-body literal and the invocation's own argument, mirroring
// #1301's own vendored_1301_lib.h DECORATE idiom. The resulting name's
// spelling location (this header, where "ab" is written) must not be
// confused with its expansion location (the invoking file), or the
// declaration is wrongly read as "already supplied by this header's own
// replayed #include" and dropped.
#ifndef MACRO_DECLARED_FN_1303_SHARED_H
#define MACRO_DECLARED_FN_1303_SHARED_H

#define V1303_MK_NAME(x) ab##x

#endif // MACRO_DECLARED_FN_1303_SHARED_H
