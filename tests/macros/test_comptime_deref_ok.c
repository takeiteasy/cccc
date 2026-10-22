// Libc calls that follow pointers stored in their arguments accept valid ones.
#include[[cccc::comptime]] < dirent.h>
#include[[cccc::comptime]] < getopt.h>
#include[[cccc::comptime]] < iconv.h>
#include[[cccc::comptime]] < regex.h>
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>
#include[[cccc::comptime]] < wchar.h>

[[cccc::comptime]]
static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::comptime]] static int g_sum;

[[cccc::comptime]]
static void visit(const void *node, VISIT which, int depth) {
    (void)depth;
    if (which == postorder || which == leaf)
        g_sum += **(int *const *)node;
}

[[cccc::comptime]] typedef struct Q {
    struct Q *next, *prev;
    int       v;
} Q;

[[cccc::comptime]]
Node *check(void) {
    int   bad    = 0;

    char  text[] = "a,b;c";
    char *save   = (char *)8; // ignored when the string is given
    int   toks   = 0;
    for (char *t = strtok_r(text, ",;", &save); t;
         t       = strtok_r(NULL, ",;", &save))
        toks++;
    bad          += toks != 3;

    iconv_t cd    = iconv_open("ASCII", "UTF-8");
    char    in[]  = "hi", out[8];
    char   *ip = in, *op = out;
    size_t  il = 2, ol = sizeof out;
    bad += iconv(cd, &ip, &il, &op, &ol) == (size_t)-1 || ol != 6;
    bad += iconv(cd, NULL, NULL, &op, &ol) == (size_t)-1;
    bad += iconv(cd, NULL, NULL, NULL, NULL) == (size_t)-1;
    iconv_close(cd);

    static int keys[4] = {2, 1, 3, 4};
    void      *root    = NULL;
    for (int i = 0; i < 4; i++)
        tsearch(&keys[i], &root, cmp_int);
    tdelete(&keys[0], &root, cmp_int);
    tsearch(&keys[0], &root, cmp_int);
    bad += !tfind(&keys[1], &root, cmp_int);
    twalk(root, visit);
    bad += g_sum != 10;
    for (int i = 0; i < 4; i++)
        tdelete(&keys[i], &root, cmp_int);
    bad += root != NULL;
    tsearch(&keys[2], &root, cmp_int);
    bad += !tfind(&keys[2], &root, cmp_int);

    hcreate(8);
    char *key = malloc(2);
    strcpy(key, "k");
    ENTRY it = {key, (void *)7};
    hsearch(it, ENTER);
    ENTRY *found  = hsearch(it, FIND);
    bad          += !found || hsearch(*found, FIND) != found;
    found->data   = (void *)9;
    bad          += (long)hsearch(it, FIND)->data != 9;
    hdestroy();

    Q a = {0}, b = {0}, c = {0};
    insque(&a, NULL);
    insque(&b, &a);
    insque(&c, &b);
    remque(&b);
    bad                   += a.next != &c || c.prev != &a;

    int           flag     = 0;
    struct option longs[]  = {{"verbose", no_argument, &flag, 1}, {0, 0, 0, 0}};
    char         *argv[]   = {"prog", "--verbose", NULL};
    bad += getopt_long(2, argv, "v", longs, NULL) != 0 || flag != 1;

    struct dirent d1 = {0}, d2 = {0};
    strcpy(d1.d_name, "a");
    strcpy(d2.d_name, "b");
    const struct dirent *p1 = &d1, *p2 = &d2;
    bad += alphasort(&p1, &p2) >= 0;

    regex_t re, badre;
    bad += regcomp(&re, "a+b", REG_EXTENDED) != 0;
    bad += regexec(&re, "xaab", 0, NULL, 0) != 0;
    regfree(&re);
    regfree(&re);
    int  rc = regcomp(&badre, "(", REG_EXTENDED);
    char msg[64];
    bad += rc == 0;
    regerror(rc, &badre, msg, sizeof msg);
    regfree(&badre);

    const char *src = "abc";
    wchar_t     wide[8];
    mbstate_t   st;
    memset(&st, 0, sizeof st);
    bad                 += mbsrtowcs(wide, &src, 8, &st) != 3 || src != NULL;
    const wchar_t *wsrc  = wide;
    char           narrow[8];
    bad += wcsrtombs(narrow, &wsrc, 8, &st) != 3 || strcmp(narrow, "abc");

    return __builtin_ast_int_literal(bad ? 100 + bad : 42);
}

int main(void) {
    return check();
}
