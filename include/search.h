/* search.h - hash table, binary tree, and linear search (POSIX) for CCCC
 *
 * ENTRY/ACTION/VISIT and hcreate/hdestroy/hsearch/lfind/lsearch/insque/
 * remque are byte- and value-identical on both platforms (verified
 * against real headers), so they need no per-platform guards and
 * register directly against the host functions.
 *
 * tsearch/tfind/tdelete take an int (*)(const void *, const void *)
 * comparator and twalk a void (*)(const void *, VISIT, int) action --
 * these go through guest-callback trampolines (src/stdlib/posix.c),
 * the same shape scandir's select/compar callbacks already use.
 *
 * twalk's nodep argument is a host-internal binary-tree node pointer
 * that the guest dereferences as *(void **)nodep to recover the actual
 * key pointer passed to tsearch() -- sound because guest and host share
 * one flat address space.
 *
 * hsearch() uses a single process-global hash table (not thread-safe);
 * glibc-only hcreate_r/hsearch_r/hdestroy_r/tdestroy are not provided.
 */

#ifndef __SEARCH_H
#define __SEARCH_H

#ifdef _WIN32
#error "<search.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"

typedef struct entry {
    char *key;
    void *data;
} ENTRY;

typedef enum {
    FIND, ENTER
} ACTION;

typedef enum {
    preorder,
    postorder,
    endorder,
    leaf
} VISIT;

extern int hcreate(size_t nel);
extern void hdestroy(void);
extern ENTRY *hsearch(ENTRY item, ACTION action);

extern void insque(void *element, void *pred);
extern void remque(void *element);

extern void *lfind(const void *key, const void *base, size_t *nmemb,
                    size_t size, int (*compar)(const void *, const void *));
extern void *lsearch(const void *key, void *base, size_t *nmemb,
                      size_t size, int (*compar)(const void *, const void *));

extern void *tdelete(const void *key, void **rootp,
                      int (*compar)(const void *, const void *));
extern void *tfind(const void *key, void *const *rootp,
                    int (*compar)(const void *, const void *));
extern void *tsearch(const void *key, void **rootp,
                      int (*compar)(const void *, const void *));
extern void twalk(const void *root, void (*action)(const void *, VISIT, int));

#endif /* __SEARCH_H */
