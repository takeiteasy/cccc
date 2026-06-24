// CCCC_FLAGS: --build
// [[cccc::build]] on a struct declaration is attribute-stripped: the struct
// is compiled and accessible in build mode.

[[cccc::build]]
struct BuildConfig {
    int jobs;
    int verbose;
};

[[cccc::build]]
int build_main(void) {
    struct BuildConfig cfg = {.jobs = 20, .verbose = 22};
    return cfg.jobs + cfg.verbose == 42 ? 42 : 1;
}
