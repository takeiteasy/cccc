// #1047 fixture: a header declaring a static global with an initializer.
// Under -c=native, this header's own text is replayed verbatim as a
// `#include`, so the global must not also be forward-declared or
// re-defined by the serializer -- it already has exactly one definition,
// supplied by this header.
#ifndef TEST_SERIALIZE_HEADER_GLOBAL_1047_H
#define TEST_SERIALIZE_HEADER_GLOBAL_1047_H

static int header_global_1047 = 40;

#endif
