static int __cccc_issignaling_f(float x) {
    union { float f; unsigned int u; } __v; __v.f = x;
    unsigned int u = __v.u;
    return ((u & 0x7F800000U) == 0x7F800000U) && (u & 0x003FFFFFU) != 0 && !(u & 0x00400000U);
}
