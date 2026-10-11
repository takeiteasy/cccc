static intmax_t __cccc_native_fromfpf(float x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl((double)x, rnd, width, 0, 0);
}
