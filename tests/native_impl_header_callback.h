// Implementation-style header: its bodies reach the includer's own functions
// and statics through config macros.
#ifdef LIB_IMPL
typedef void (*lib_cb)(int);

static int lib_run(int x) {
    LIB_LOG(x);
    LIB_COUNT++;
    lib_cb cb = LIB_CB;
    cb(x);
    return x;
}
#endif
