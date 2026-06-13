// Test C23 const-correct search functions in <string.h> (ticket #396)
// strchr, strrchr, strstr, strpbrk, memchr preserve const-ness of their
// pointer argument in the return type via _Generic dispatch macros.
#include <string.h>

int main(void) {
    /* const path: result must be assignable to const pointer */
    const char *s = "hello world";
    const char *p1 = strchr(s, 'o');
    const char *p2 = strrchr(s, 'o');
    const char *p3 = strstr(s, "world");
    const char *p4 = strpbrk(s, "aeiou");
    /* memchr always returns void * (implicitly assignable to const void *) */
    void *p5 = memchr(s, 'o', 11);
    if (!p1 || !p2 || !p3 || !p4 || !p5) return 1;

    /* check correctness of returned positions */
    if (*p1 != 'o') return 2;           /* first 'o' in "hello world" */
    if (*(p1 + 1) != ' ') return 3;     /* 'o' at index 7, next is space */
    if (*p2 != 'o') return 4;           /* last 'o' in "hello world" */
    if (p2 <= p1) return 5;             /* last 'o' is after first 'o' */
    if (*p3 != 'w') return 6;           /* strstr points to "world" */
    if (*p4 != 'e') return 7;           /* first vowel in "hello world" is 'e' */

    /* non-const path: result must be assignable to non-const pointer */
    char buf[] = "hello world";
    char *q1 = strchr(buf, 'o');
    char *q2 = strrchr(buf, 'o');
    char *q3 = strstr(buf, "world");
    char *q4 = strpbrk(buf, "aeiou");
    void *q5 = memchr(buf, 'o', 11);
    if (!q1 || !q2 || !q3 || !q4 || !q5) return 8;
    (void)q5;

    /* verify non-const result is writable */
    *q1 = 'O';
    if (buf[4] != 'O') return 9;   /* replaced 'o' in "hellO world" */

    /* strchr returning NULL when not found */
    if (strchr(s, 'z') != 0) return 10;
    if (strstr(s, "xyz") != 0) return 11;
    if (strpbrk(s, "xyz") != 0) return 12;

    return 42;
}
