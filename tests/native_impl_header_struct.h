// Implementation-style header whose body calls an includer function that
// takes an includer-defined struct.
#ifdef LIB_IMPL
static int lib_run(void) { return LIB_GET(0); }
#endif
