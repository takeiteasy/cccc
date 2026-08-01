// Ticket V010: op_CALLN_fn (the indirect-call opcode used when a guest
// function pointer variable holds an FFI token) computed the argument
// count for a registered variadic FFI function from ff->num_args, which
// for a variadic registration equals num_fixed_args -- the variadic tail
// was silently dropped, and float_arg_mask/the fixed-vs-variadic register
// split were ignored entirely (#874). This regression-tests the fix: call
// a variadic FFI function through a function pointer with (a) an integer
// tail argument, (b) a double tail argument (the register-file case --
// fixed flonum params live in FREG_A0+, but variadic-tail doubles are
// bit-patterns in REG_A0+ for va_arg to spill), and (c) more than 8
// arguments (stack-passed tail).
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Fixed double params through a function pointer: sprintf's fixed
    // params below are all int-class, so they never exercise the FREG_A0+
    // read path in the shared marshalling loop -- pow() does. This was
    // also broken before the fix: the old FFI-token branch read only
    // vm->regs[REG_A0+i] and ignored float_arg_mask entirely, so a fixed
    // double param through a function pointer got int-register garbage
    // handed to libffi as a double.
    double (*powp)(double, double) = pow;
    if (powp(2.0, 10.0) != 1024.0) return 4;

    int (*p)(char *, const char *, ...) = sprintf;
    char buf[128];

    // (a) integer tail arg
    p(buf, "%d", 7);
    if (strcmp(buf, "7") != 0) return 1;

    // (b) double tail arg -- must come from the integer register file, not
    // FREG_A0 (which is where a *fixed* flonum param would live).
    p(buf, "%d-%f", 7, 3.5);
    if (strcmp(buf, "7-3.500000") != 0) return 2;

    // (c) >8 arguments total, mixing fixed + variadic-tail ints/doubles to
    // push some onto the VM stack.
    p(buf, "%d %d %d %d %d %d %d %d %f", 1, 2, 3, 4, 5, 6, 7, 8, 9.5);
    if (strcmp(buf, "1 2 3 4 5 6 7 8 9.500000") != 0) return 3;

    return 42;
}
