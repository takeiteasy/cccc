# glibc <obstack.h> in the VM: the obstack natives, guest chunk callbacks and
# obstack_printf, plain and fortified. Skipped without glibc's header.
. "$(dirname "$0")/lib.sh"

[ -f /usr/include/obstack.h ] || { echo "skip: no glibc obstack.h"; exit 0; }

cat >plain.c <<'C'
#define _GNU_SOURCE
#include <obstack.h>
#include <stdlib.h>
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free
int main(void) {
    struct obstack o;
    obstack_init(&o);
    obstack_free(&o, NULL);
    return 42;
}
C

cat >guest_cb.c <<'C'
#define _GNU_SOURCE
#include <obstack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int allocs, frees;
static void *counting_alloc(size_t n) {
    allocs++;
    return malloc(n);
}
static void counting_free(void *p) {
    frees++;
    free(p);
}
#define obstack_chunk_alloc counting_alloc
#define obstack_chunk_free counting_free

int main(void) {
    struct obstack o;
    obstack_begin(&o, 16);
    for (int i = 0; i < 100; i++)
        obstack_grow(&o, "abcdefgh", 8);
    obstack_1grow(&o, 0);
    char *s = obstack_finish(&o);
    if (strlen(s) != 800 || strncmp(s, "abcdefghabcdefgh", 16))
        return 1;
    if (obstack_printf(&o, "%s-%d", "x", 7) != 3)
        return 2;
    obstack_1grow(&o, 0);
    char *t = obstack_finish(&o);
    if (strcmp(t, "x-7"))
        return 3;
    if (allocs < 2 || obstack_memory_used(&o) < 800)
        return 4;
    obstack_free(&o, NULL);
    return frees == allocs ? 42 : 5;
}
C

cat >malloc_cb.c <<'C'
#define _GNU_SOURCE
#include <obstack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

int main(void) {
    struct obstack o;
    obstack_init(&o);
    for (int i = 0; i < 1000; i++)
        obstack_grow(&o, "0123456789", 10);
    obstack_1grow(&o, 0);
    char *s = obstack_finish(&o);
    int   ok = strlen(s) == 10000;
    obstack_printf(&o, "%05d", 42);
    obstack_1grow(&o, 0);
    ok = ok && !strcmp(obstack_finish(&o), "00042");
    obstack_free(&o, NULL);
    return ok ? 42 : 1;
}
C

cat >reinit.c <<'C'
#define _GNU_SOURCE
#include <obstack.h>
#include <stdlib.h>
#include <string.h>

static int allocs, frees;
static void *counting_alloc(size_t n) {
    allocs++;
    return malloc(n);
}
static void counting_free(void *p) {
    frees++;
    free(p);
}
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

int main(void) {
    struct obstack o;
    obstack_init(&o);
    obstack_chunkfun(&o, counting_alloc);
    obstack_freefun(&o, counting_free);
    for (int i = 0; i < 1000; i++)
        obstack_grow(&o, "0123456789", 10);
    if (allocs == 0)
        return 1;
    obstack_free(&o, NULL);
    if (frees == 0)
        return 2;
    obstack_init(&o);
    obstack_alloc(&o, 10);
    obstack_free(&o, NULL);
    return 42;
}
C

cat >arg_cb.c <<'C'
#define _GNU_SOURCE
#include <obstack.h>
#include <stdlib.h>

static int  arena = 7, seen;
static void *alloc_with(void *arg, size_t n) {
    seen += *(int *)arg;
    return malloc(n);
}
static void free_with(void *arg, void *p) {
    seen += *(int *)arg;
    free(p);
}

int main(void) {
    struct obstack o;
    obstack_specify_allocation_with_arg(&o, 64, 0, alloc_with, free_with, &arena);
    obstack_alloc(&o, 40);
    obstack_alloc(&o, 40);
    obstack_free(&o, NULL);
    return seen >= 28 ? 42 : seen;
}
C

expect_exit 42 "$CCCC" --sysroot / --use-system-headers reinit.c
expect_exit 42 "$CCCC" --sysroot / --use-system-headers arg_cb.c
expect_exit 42 "$CCCC" --sysroot / --use-system-headers plain.c
expect_exit 42 "$CCCC" --sysroot / --use-system-headers guest_cb.c
expect_exit 42 "$CCCC" --sysroot / --use-system-headers malloc_cb.c
expect_exit 42 "$CCCC" --sysroot / --use-system-headers -O1 -D_FORTIFY_SOURCE=2 malloc_cb.c
