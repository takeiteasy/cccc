// Implementation-style header whose body calls includer functions that take
// includer-defined types.
#ifdef LIB_IMPL
static int lib_run(void) {
    return LIB_A() + LIB_B() + LIB_C() + LIB_D();
}
#endif
