/* ccccl_rt_internal.h — the real LObj layout.
 *
 * Included only by ccccl_rt.c. Never reaches cccc's comptime pass and never
 * reaches a hand-written host TU — see the file comment in ccccl_rt.h for
 * why LObj is opaque everywhere else.
 */
#ifndef CCCCL_RT_INTERNAL_H
#define CCCCL_RT_INTERNAL_H

#include "ccccl_rt.h"

#ifndef CCCCL_RT_ARENA_CELLS
#define CCCCL_RT_ARENA_CELLS 65536
#endif
#ifndef CCCCL_RT_MAX_SYMS
#define CCCCL_RT_MAX_SYMS 1024
#endif
#ifndef CCCCL_RT_MAX_INTS
#define CCCCL_RT_MAX_INTS 65536
#endif

typedef enum LObjTag {
    CCCCL_ATOM,
    CCCCL_PAIR,
    CCCCL_INT,
    CCCCL_CLOSURE
} LObjTag;

typedef struct {
    const char *name;
} LObjAtom;
typedef struct {
    LObj *car, *cdr;
} LObjPair;
typedef struct {
    CccclFn fn;
    LObj   *captures;
} LObjClosure;

struct LObj {
    LObjTag tag;
    union {
        LObjAtom    atom;
        LObjPair    pair;
        long long   ival;
        LObjClosure closure;
    } as;
};

#endif /* CCCCL_RT_INTERNAL_H */
