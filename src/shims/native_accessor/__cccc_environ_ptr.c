#undef environ
extern char **environ;
static char ***__cccc_environ_ptr(void) { return &environ; }
