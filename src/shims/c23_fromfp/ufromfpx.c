static uintmax_t __cccc_native_ufromfpx(double x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl(x, rnd, width, 1, 1);
}
