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

typedef enum LObjTag { CCCCL_ATOM, CCCCL_PAIR, CCCCL_FN } LObjTag;

/* atom.name is a pointer into a private string pool (see ccccl_intern in
 * ccccl_rt.c), not a fixed-size buffer: symbol names have no length cap,
 * and LObj stays three words regardless of how long an interned name is --
 * a fixed inline buffer would size every LObj (cons cells included) to the
 * longest symbol name allowed. Every union member below is a named typedef
 * rather than an inline anonymous struct, which is no longer required now
 * that LObj is fully opaque outside this file (nothing else parses this
 * struct at all), but there is no reason to revert something this cheap. */
typedef struct {
    const char *name;
} LObjAtom;
typedef struct {
    LObj *car, *cdr;
} LObjPair;
typedef struct {
    CccclFn fn;
    LObj   *env;
} LObjClosure;

struct LObj {
    LObjTag tag;
    union {
        LObjAtom    atom;
        LObjPair    pair;
        LObjClosure closure;
    } as;
};

#endif /* CCCCL_RT_INTERNAL_H */
