#include <stdio.h>

#ifndef BENCH_N
#define BENCH_N 1000000
#endif

enum { OP_ADD, OP_MUL, OP_XOR, OP_ROT, OP_DEC, OP_LOOP, OP_HALT };

#define NEXT goto *labels[*pc++]

int main(void) {
    static void *labels[] = {&&op_add, &&op_mul,  &&op_xor, &&op_rot,
                             &&op_dec, &&op_loop, &&op_halt};
    static const unsigned char code[] = {OP_ADD, OP_MUL, OP_XOR,  OP_ROT,
                                         OP_ADD, OP_DEC, OP_LOOP, OP_HALT};
    unsigned long              acc    = 1;
    long                       left   = BENCH_N;
    const unsigned char       *pc     = code;
    NEXT;
op_add:
    acc += 0x9E3779B9UL;
    NEXT;
op_mul:
    acc *= 6364136223846793005UL;
    NEXT;
op_xor:
    acc ^= acc >> 29;
    NEXT;
op_rot:
    acc = (acc >> 3) | (acc << 61);
    NEXT;
op_dec:
    left--;
    NEXT;
op_loop:
    if (left > 0)
        pc = code;
    NEXT;
op_halt:
    printf("result: %lu\n", acc);
    return 42;
}
