static uintmax_t __cccc_native_ufromfpxl(long double x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl((double)x, rnd, width, 1, 1);
}
