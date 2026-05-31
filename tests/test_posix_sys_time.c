#include <sys/time.h>
#include <unistd.h>

int main(void) {
    struct timeval tv1, tv2;
    if (gettimeofday(&tv1, 0) != 0) return 1;

    usleep(10000); /* 10ms */

    if (gettimeofday(&tv2, 0) != 0) return 2;

    /* tv2 should be >= tv1 */
    if (tv2.tv_sec < tv1.tv_sec) return 3;
    if (tv2.tv_sec == tv1.tv_sec && tv2.tv_usec < tv1.tv_usec) return 4;

    /* timeradd / timersub macros */
    struct timeval a = { 1, 500000 };
    struct timeval b = { 2, 600000 };
    struct timeval res;
    timeradd(&a, &b, &res);
    if (res.tv_sec != 4 || res.tv_usec != 100000) return 5;

    timersub(&b, &a, &res);
    if (res.tv_sec != 1 || res.tv_usec != 100000) return 6;

    return 42;
}
