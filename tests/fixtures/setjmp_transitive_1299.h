// Fixture for tests/test_serialize_setjmp_transitive_1299.c (#1299).
//
// Deliberately an ordinary project header that itself #includes <setjmp.h>
// -- mirrors src/cccc.h's own shape, the real-world case #1132's round-13
// self-hosting spike hit. No include guard needed for that shape: the point
// is that this header's own #include is replayed as ONE line, and the host
// re-reads its real content (including the nested <setjmp.h>) straight off
// disk once captured.
#include <setjmp.h>

typedef struct {
    int x;
} setjmp_transitive_1299_dummy;
