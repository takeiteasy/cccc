// Ticket #1040: stdout/stderr accessor shims must not collide with
// include/stdio.h's own extern declaration under -c=native/-c=generated --
// confirmed with a minimal `fprintf(stdout, ...)` repro before the fix
// (include/stdio.h's unconditional `extern FILE* __cccc_stdout(void);`
// clashed with the `static` definition serialize.c's native_accessor_shims
// emits, "static declaration ... follows non-static declaration").

#include <stdio.h>

static int emit(FILE *out, const char *msg) {
    return fprintf(out, "%s\n", msg);
}

int main(void) {
    if (emit(stdout, "hi") <= 0)
        return 1;
    if (emit(stderr, "err") <= 0)
        return 2;
    return 42;
}
