// stb-style header: declarations first, bodies once LIB_IMPL is defined.
#ifndef LIB_DECL_H
#define LIB_DECL_H
typedef struct {
    int v;
} lib_t;
int lib_run(lib_t *t);
#endif
#ifdef LIB_IMPL
int lib_run(lib_t *t) {
    LIB_LOG(t);
    return t->v;
}
#endif
int lib_decl_trailer;
