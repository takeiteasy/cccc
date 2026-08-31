// #1241 fixture: a header included via `#include @shared`, defining a
// typedef and a tagged struct that a comptime-generated global's element
// type resolves to. Shared between the runtime TU and comptime execution,
// so GetType() resolves both names -- the bug this file exercises is about
// where the generated `-c=generated` output places the #include that
// replays this header relative to the global's own forward declaration,
// not about type resolution itself.
#ifndef TEST_SERIALIZE_SHARED_TYPEDEF_1241_H
#define TEST_SERIALIZE_SHARED_TYPEDEF_1241_H

typedef unsigned char MyByte1241;

struct MyPair1241 {
    int a;
    int b;
};

#endif
