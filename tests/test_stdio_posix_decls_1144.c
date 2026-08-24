// #1144: popen/pclose, fseeko/ftello, flockfile/funlockfile, and
// getc_unlocked -- registered VM cfuncs (src/stdlib/stdio.c) that had no
// declaration in CCCC's bundled <stdio.h> at all -- found auditing every
// cc_register_cfunc name against every bundled header's own declarations
// once implicit function declaration became a hard error at C99+/
// -c=native. Exercises them with only #include <stdio.h> in scope, on
// both the VM and -c=native.
#include <stdio.h>

int main(void) {
    FILE *p = popen("echo hi", "r");
    if (!p)
        return 1;
    char buf[16] = {0};
    if (!fgets(buf, sizeof(buf), p))
        return 2;
    if (pclose(p) == -1)
        return 3;

    FILE *f = tmpfile();
    if (!f)
        return 4;
    if (fputs("abc", f) < 0)
        return 5;
    if (fseeko(f, 0, 0 /* SEEK_SET */) != 0)
        return 6;
    if (ftello(f) != 0)
        return 7;

    flockfile(f);
    int c = getc_unlocked(f);
    funlockfile(f);
    if (c != 'a')
        return 8;

    fclose(f);
    return 42;
}
