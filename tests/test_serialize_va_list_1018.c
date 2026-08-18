// Ticket #1018: <stdarg.h>'s va_start/va_arg/va_copy/va_end macros used to
// expand directly into VM-ABI pointer arithmetic over CCCC's own struct
// va_list (reg_ptr/stack_ptr/reg_count) and __builtin_frame_address(0).
// The serializer printed that expansion verbatim under -c=native/-m, which
// a real host compiler rejects outright ("member reference base type
// 'va_list' (aka 'char *') is not a structure or union") the moment the
// replayed `#include <stdarg.h>` line resolves to the real, differently-
// shaped host va_list at native-compile time.
//
// Fixed by wrapping each macro's existing VM-ABI expansion (unchanged) as
// the trailing argument to a new internal __cccc_va_start/_arg/_copy/_end
// builtin (src/parse_postfix.c) that parses ap/last/type/src a second,
// independent time purely to stamp them as serializer annotation
// (Node.va_form, src/cccc.h) on the returned, otherwise-identical impl
// node. VM codegen/comptime/reflection/inlining see byte-identical AST
// throughout -- confirmed by this file passing on the VM path both before
// and after the fix; only the serializer's own printed text changed.
//
// Exercises all four macros plus nesting (an inner variadic call from
// inside an outer variadic function's own loop, #1018's own repro shape)
// and va_copy (a second independent walk of the same argument list).

#include "stdarg.h"

static double sum_doubles(int count, ...) {
    va_list args;
    va_start(args, count);

    // va_copy: walk a second, independent copy of the same list first.
    va_list copy;
    va_copy(copy, args);
    double copy_total = 0.0;
    for (int i = 0; i < count; i++)
        copy_total += va_arg(copy, double);
    va_end(copy);

    double total = 0.0;
    for (int i = 0; i < count; i++)
        total += va_arg(args, double);
    va_end(args);

    if (copy_total != total)
        return -1.0; // the two independent walks must agree

    return total;
}

static int sum_ints(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

// A variadic function called from inside another variadic function's own
// va_arg loop -- #1018's own "nested" repro shape (tests/repro_nested2.c
// etc): each has its own independent va_start/va_end, not a shared or
// forwarded va_list.
static int outer_vararg(int count, ...) {
    va_list args;
    va_start(args, count);
    int total = 0;
    for (int i = 0; i < count; i++) {
        int val = va_arg(args, int);
        total += sum_ints(2, val, val * 2);
    }
    va_end(args);
    return total;
}

int main() {
    double d = sum_doubles(4, 1.0, 2.0, 3.0, 4.0);
    if (d != 10.0)
        return 1;

    // outer_vararg(3, 10, 20, 30): each val contributes val + val*2 = 3*val
    // via the nested sum_ints call -- 3*10 + 3*20 + 3*30 = 180.
    int i = outer_vararg(3, 10, 20, 30);
    if (i != 180)
        return 2;

    return 42;
}
