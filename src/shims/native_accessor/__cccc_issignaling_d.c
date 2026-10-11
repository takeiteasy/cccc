static int __cccc_issignaling_d(double x) {
    union { double d; unsigned long long u; } __v; __v.d = x;
    unsigned long long u = __v.u;
    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (u & 0x0007FFFFFFFFFFFFULL) != 0 && !(u & 0x0008000000000000ULL);
}
