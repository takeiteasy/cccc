#ifndef LIB_H
#define LIB_H
int lib_run(void);
#endif

#ifdef LIB_IMPL
int lib_run(void) {
    return 42;
}
#endif
