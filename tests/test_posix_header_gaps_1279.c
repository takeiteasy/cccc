// Expected return: 42
// #1279: a batch of POSIX symbols CCCC's own source uses that resolved
// against the real system headers under a plain `make` build but were
// missing from the bundled include/ copy (self-hosting spike, #1132):
// strtok_r, strtok (declared but never FFI-registered), strsignal, PATH_MAX,
// realpath, ctime_r, open_memstream, plus strcasecmp used without its header.
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int main(void) {
    // strtok_r + strtok
    char        buf1[]  = "a,bb,ccc";
    char       *save    = 0;
    const char *want[3] = {"a", "bb", "ccc"};
    int         n       = 0;
    for (char *t = strtok_r(buf1, ",", &save); t; t = strtok_r(0, ",", &save)) {
        if (n >= 3 || strcmp(t, want[n]) != 0)
            return 1;
        n++;
    }
    if (n != 3)
        return 2;

    char buf2[] = "x-y";
    if (strcmp(strtok(buf2, "-"), "x") != 0)
        return 3;
    if (strcmp(strtok(0, "-"), "y") != 0)
        return 4;

    // strsignal -- non-NULL, non-empty description
    const char *s = strsignal(SIGINT);
    if (!s || !*s)
        return 5;

    // strcasecmp (from <strings.h>)
    if (strcasecmp("HeLLo", "hello") != 0)
        return 6;
    if (strcasecmp("abc", "abd") == 0)
        return 7;

    // PATH_MAX + realpath
    if (PATH_MAX < 256)
        return 8;
    char  resolved[PATH_MAX];
    char *r = realpath(".", resolved);
    if (r != resolved || resolved[0] != '/')
        return 9;

    // ctime_r -- caller-owned 26-byte buffer
    time_t now = time(0);
    char   tb[26];
    char  *ct = ctime_r(&now, tb);
    if (ct != tb || strlen(tb) < 20)
        return 10;

    // open_memstream -- heap-backed write stream
    char  *mbuf = 0;
    size_t msz  = 0;
    FILE  *ms   = open_memstream(&mbuf, &msz);
    if (!ms)
        return 11;
    fputs("hello world", ms);
    fflush(ms);
    if (msz != 11 || !mbuf || strcmp(mbuf, "hello world") != 0) {
        fclose(ms);
        free(mbuf);
        return 12;
    }
    fclose(ms);
    free(mbuf);

    return 42;
}
