#include <setjmp.h>

static jmp_buf _ex_buf;
static int     _ex_code;

#define TRY      if ((_ex_code = setjmp(_ex_buf)) == 0)
#define CATCH(e) else if (_ex_code == (e))
#define THROW(e) longjmp(_ex_buf, (e))

int main(void) {
    int caught = 0;
    TRY { THROW(1); } CATCH(1) { caught = 1; }
    if (caught != 1) return 1;

    int reached = 0;
    TRY { THROW(2); reached = 1; } CATCH(2) { /* ok */ }
    if (reached) return 2;

    /* nested: inner throw is caught inside outer TRY body */
    int outer = 0, inner = 0;
    TRY {
        TRY { THROW(3); } CATCH(3) { inner = 1; }
        outer = 1;
    } CATCH(99) { return 3; }
    if (!outer || !inner) return 4;

    return 42;
}
