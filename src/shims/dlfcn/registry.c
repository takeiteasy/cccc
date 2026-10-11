#include <dlfcn.h>
#include <stdlib.h>
struct __cccc_dl_node {
    void *handle;
    int live;
    int closed;
    struct __cccc_dl_node *next;
};
static struct __cccc_dl_node *__cccc_dl_head = 0;
static _Thread_local char *__cccc_dl_error = 0;
static struct __cccc_dl_node *__cccc_dl_find(void *token) {
    for (struct __cccc_dl_node *n = __atomic_load_n(&__cccc_dl_head, __ATOMIC_ACQUIRE); n;
         n = n->next)
        if ((void *)n == token && !__atomic_load_n(&n->closed, __ATOMIC_ACQUIRE))
            return n;
    return 0;
}
static void __cccc_dl_push(struct __cccc_dl_node *n) {
    struct __cccc_dl_node *old_head;
    do {
        old_head = __atomic_load_n(&__cccc_dl_head, __ATOMIC_ACQUIRE);
        n->next = old_head;
    } while (!__atomic_compare_exchange_n(&__cccc_dl_head, &old_head, n, 0,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
}
