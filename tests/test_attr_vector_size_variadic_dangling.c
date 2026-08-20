// CCCC_FLAGS: -3
// Ticket #727: two differently-shaped variadic functions that each read a
// vector argument via va_arg, called in sequence, used to trip a false
// positive in the -3 dangling-pointer detector. sum_vecs's own `v` local is
// never explicitly address-taken (escape analysis proves it never leaves
// the frame), so emit_lea3_var used to skip STKTAG-ing it entirely; reading
// past its first lane (v[1], v[2], v[3]) then fell through to
// stack_interval_stab, which could match a dead sibling call's own
// escaping-aggregate range (mixed's or sum_vecs's own `va_list ap`) that
// happened to physically overlap `v`'s stack slot after that sibling
// returned. Fixed by always STKTAG-ing a vector local's full extent
// regardless of what escape analysis proved (emit_lea3_var, #727), plus a
// prefer-live resolution in stack_interval_stab so a live STKTAG range
// always wins over an overlapping dead one at the same address.
#include <stdarg.h>

typedef float v4sf __attribute__((vector_size(16)));

// Reads more than one vector via va_arg in a loop -- each iteration reuses
// the same non-escaping `v` slot, and readback of v[1..3] is what used to
// fall through to the interior-pointer stab.
static int sum_vecs(int n, ...) {
    va_list ap;
    va_start(ap, n);
    float acc = 0;
    for (int i = 0; i < n; i++) {
        v4sf v  = va_arg(ap, v4sf);
        acc    += v[0] + v[1] + v[2] + v[3];
    }
    va_end(ap);
    return (int)acc;
}

// A differently-shaped sibling call (interleaved scalar + vector), called
// right after sum_vecs returns -- its own `va_list ap` (an escaping struct)
// reuses the stack addresses sum_vecs's dead frame occupied, including
// wherever sum_vecs's non-escaping `v` used to live.
static int mixed(int n, ...) {
    va_list ap;
    va_start(ap, n);
    v4sf v  = va_arg(ap, v4sf);
    int  i2 = va_arg(ap, int);
    va_end(ap);
    return (int)(v[0] + v[1] + v[2] + v[3]) + i2;
}

int main(void) {
    v4sf a = {1, 2, 3, 4}, b = {10, 20, 30, 40};

    if (sum_vecs(2, a, b) != 110)
        return 1;
    if (mixed(0, a, 100) != 110)
        return 2;

    return 42;
}
