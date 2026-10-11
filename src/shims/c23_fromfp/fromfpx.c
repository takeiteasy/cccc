static intmax_t __cccc_native_fromfpx(double x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl(x, rnd, width, 0, 1);
}
