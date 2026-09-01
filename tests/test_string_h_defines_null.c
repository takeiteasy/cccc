// C requires <string.h> (7.24.1) to define NULL. CCCC's include/string.h
// declared its functions but pulled in nothing, so `#include <string.h>`
// alone left NULL undefined -- `char *p = NULL;` failed with "undefined
// variable 'NULL'". It now includes <stddef.h> (as does include/locale.h,
// which had the same gap).
//
// #ifndef/#error so this fails to *compile* against the pre-fix header
// rather than passing vacuously.
//
// No `#include <stddef.h>` here on purpose: the point is that <string.h>
// provides NULL on its own.

#include <string.h>
#ifndef NULL
#error "<string.h> must define NULL"
#endif

int main(void) {
    char *p = NULL;
    char  buf[4] = {1, 2, 3, 0};

    if (p != NULL)
        return 1;
    if (memchr(buf, 9, 4) != NULL) // no 9 in buf -> NULL
        return 2;
    p = buf;
    if (memchr(p, 2, 4) != p + 1)
        return 3;

    return 42;
}
