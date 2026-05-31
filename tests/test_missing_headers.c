#include <iso646.h>
#include <locale.h>
#include <signal.h>

int main(void) {
    int a = 1;
    int b = 0;
    if ((a and not b) != 1) return 1;
    a and_eq 1;
    b or_eq 2;
    if (a != 1 or b != 2) return 2;

    if (!setlocale(LC_ALL, "C")) return 3;
    struct lconv *lc = localeconv();
    if (!lc || !lc->decimal_point) return 4;

    if (signal(SIGTERM, SIG_IGN) == SIG_ERR) return 5;
    if (signal(SIGTERM, SIG_DFL) == SIG_ERR) return 6;

    return 42;
}
