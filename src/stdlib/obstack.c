// obstack.c -- glibc <obstack.h> natives.
//
// The inline macros in glibc's header work on `struct obstack` in guest
// memory and call these out of line. The chunk allocator and deallocator are
// guest function values (a bytecode offset or a native token) kept in the
// struct itself, so they are called through cccc_call_guest_callback rather
// than as host pointers.
#include "../cccc.h"
#include "../internal.h"

#ifdef __GLIBC__
#include <obstack.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBSTACK_DEFAULT_ALIGNMENT 16
#define OBSTACK_DEFAULT_CHUNK     4096

static long long guest_fn(void (*fn)(void)) {
    return (long long)(intptr_t)fn;
}

// TODO: glibc runs obstack_alloc_failed_handler here; guest assignments to it
// are not reachable from the VM (#1851)
static void obstack_exhausted(void) {
    fputs("memory exhausted\n", stderr);
    exit(1);
}

static struct _obstack_chunk *obstack_new_chunk(struct obstack *h, long size) {
    long long args[2];
    long long rv = 0;
    int       n  = 0;
    if (h->use_extra_arg)
        args[n++] = (long long)(intptr_t)h->extra_arg;
    args[n++] = size;
    if (cccc_call_guest_callback(cccc_current_ffi_vm(),
                                 guest_fn((void (*)(void))h->chunkfun), args, n,
                                 &rv) != 0 ||
        !rv)
        obstack_exhausted();
    return (struct _obstack_chunk *)rv;
}

static void obstack_drop_chunk(struct obstack        *h,
                               struct _obstack_chunk *chunk) {
    long long args[2];
    long long rv = 0;
    int       n  = 0;
    if (h->use_extra_arg)
        args[n++] = (long long)(intptr_t)h->extra_arg;
    args[n++] = (long long)(intptr_t)chunk;
    cccc_call_guest_callback(cccc_current_ffi_vm(),
                             guest_fn((void (*)(void))h->freefun), args, n,
                             &rv);
}

static char *obstack_align(struct obstack *h, struct _obstack_chunk *chunk) {
    uintptr_t p = (uintptr_t)chunk->contents;
    return (char *)((p + (uintptr_t)h->alignment_mask) &
                    ~(uintptr_t)h->alignment_mask);
}

static long long obstack_begin_with(long long hv, long long size,
                                    long long align, long long chunkfun,
                                    long long freefun, long long arg,
                                    bool has_arg) {
    struct obstack *h = (struct obstack *)hv;
    if (align == 0)
        align = OBSTACK_DEFAULT_ALIGNMENT;
    if (size == 0)
        size = OBSTACK_DEFAULT_CHUNK -
               offsetof(struct _obstack_chunk, contents) -
               OBSTACK_DEFAULT_ALIGNMENT;
    h->chunk_size     = (long)size;
    h->alignment_mask = (int)align - 1;
    h->chunkfun = (struct _obstack_chunk * (*)(void *, long))(intptr_t)chunkfun;
    h->freefun  = (void (*)(void *, struct _obstack_chunk *))(intptr_t)freefun;
    h->extra_arg                 = (void *)(intptr_t)arg;
    h->use_extra_arg             = has_arg;
    h->maybe_empty_object        = 0;
    h->alloc_failed              = 0;

    struct _obstack_chunk *chunk = obstack_new_chunk(h, h->chunk_size);
    h->chunk                     = chunk;
    h->object_base = h->next_free = obstack_align(h, chunk);
    h->chunk_limit = chunk->limit = (char *)chunk + h->chunk_size;
    chunk->prev                   = NULL;
    return 1;
}

static long long wrap_obstack_begin(long long h, long long size,
                                    long long align, long long chunkfun,
                                    long long freefun) {
    return obstack_begin_with(h, size, align, chunkfun, freefun, 0, false);
}

static long long wrap_obstack_begin_1(long long h, long long size,
                                      long long align, long long chunkfun,
                                      long long freefun, long long arg) {
    return obstack_begin_with(h, size, align, chunkfun, freefun, arg, true);
}

// Moves the object being built into a chunk with room for `length` more bytes.
static long long wrap_obstack_newchunk(long long hv, long long length) {
    struct obstack        *h        = (struct obstack *)hv;
    struct _obstack_chunk *old      = h->chunk;
    long                   obj_size = (long)(h->next_free - h->object_base);
    long                   new_size =
        obj_size + (long)length + obj_size / 8 + h->alignment_mask + 100;
    if (new_size < h->chunk_size)
        new_size = h->chunk_size;

    struct _obstack_chunk *chunk = obstack_new_chunk(h, new_size);
    h->chunk                     = chunk;
    chunk->prev                  = old;
    h->chunk_limit = chunk->limit = (char *)chunk + new_size;
    char *base                    = obstack_align(h, chunk);
    memcpy(base, h->object_base, (size_t)obj_size);

    // The old chunk held nothing but this object: release it.
    if (!h->maybe_empty_object && h->object_base == obstack_align(h, old)) {
        chunk->prev = old->prev;
        obstack_drop_chunk(h, old);
    }
    h->object_base        = base;
    h->next_free          = base + obj_size;
    h->maybe_empty_object = 0;
    return 0;
}

static long long wrap_obstack_memory_used(long long hv) {
    struct obstack *h     = (struct obstack *)hv;
    long long       total = 0;
    for (struct _obstack_chunk *c = h->chunk; c; c = c->prev)
        total += c->limit - (char *)c;
    return total;
}

// Frees every chunk back to the one holding `obj`; NULL frees them all.
static long long wrap_obstack_free(long long hv, long long objv) {
    struct obstack        *h   = (struct obstack *)hv;
    char                  *obj = (char *)(intptr_t)objv;
    struct _obstack_chunk *c   = h->chunk;
    while (c && !(obj > (char *)c && obj <= c->limit)) {
        struct _obstack_chunk *prev = c->prev;
        obstack_drop_chunk(h, c);
        c                     = prev;
        h->maybe_empty_object = 1;
    }
    if (c) {
        h->object_base = h->next_free = obj;
        h->chunk_limit                = c->limit;
        h->chunk                      = c;
    } else if (obj) {
        abort();
    } else {
        h->chunk       = NULL;
        h->object_base = h->next_free = h->chunk_limit = NULL;
    }
    return 0;
}

void register_obstack_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "_obstack_begin", (void *)wrap_obstack_begin, 5, 0);
    cc_register_cfunc(vm, "_obstack_begin_1", (void *)wrap_obstack_begin_1, 6,
                      0);
    cc_register_cfunc(vm, "_obstack_newchunk", (void *)wrap_obstack_newchunk, 2,
                      0);
    cc_register_cfunc(vm, "_obstack_memory_used",
                      (void *)wrap_obstack_memory_used, 1, 0);
    cc_register_cfunc(vm, "obstack_free", (void *)wrap_obstack_free, 2, 0);
}

#else
void register_obstack_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
