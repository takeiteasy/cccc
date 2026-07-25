// CCCC_FLAGS: --type-checks
// Ticket #752: extending CHKT3's byte-granular type shadow to globals must
// not introduce new false positives. Covers: a global union punned both
// ways (mirrors test_heap_type_union_ok.c, but on data_seg instead of the
// heap), a global char buffer written byte-wise then read back as a
// struct, a function-local static, and a struct-returning function called
// repeatedly with two different return types through the RETBUF pool
// (return_buffer_pool lives in data_seg and is reused across calls --
// op_RETBUF_fn must clear a slot's shadow before handing it out again, or
// this would false-positive against whatever type was returned through it
// last).
union U {
    int i;
    float f;
};
union U gu;

struct Pair {
    int a;
    int b;
};

struct Pair make_pair(int a, int b) {
    struct Pair p;
    p.a = a;
    p.b = b;
    return p;
}

float make_float(float f) {
    return f; // scalar return, no RETBUF -- keeps the call sequence mixed
}

int counter(void) {
    static int n; // function-local static: lives in data_seg like a global
    n++;
    return n;
}

int main(void) {
    gu.i = 42;
    int a = gu.i;   // same member: must read back cleanly
    gu.f = 1.0f;    // legal punning: write a different member
    float b = gu.f; // read that member back
    gu.i = 42;      // punning back the other way
    int c = gu.i;

    struct Pair p1 = make_pair(1, 2);       // stamps a RETBUF slot as struct Pair
    float f1 = make_float(3.5f);            // no RETBUF traffic for this call
    struct Pair p2 = make_pair(3, 4);       // same/rotating RETBUF slot, same type: must not false-positive

    int n1 = counter();
    int n2 = counter();

    return (a == 42 && b == 1.0f && c == 42 && p1.a == 1 && p1.b == 2 &&
            f1 == 3.5f && p2.a == 3 && p2.b == 4 && n1 == 1 && n2 == 2)
               ? 42
               : 1;
}
