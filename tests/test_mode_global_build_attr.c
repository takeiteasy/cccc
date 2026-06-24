// CCCC_FLAGS: --build
// [[cccc::build]] on a global variable is attribute-stripped: the global is
// compiled and accessible in build mode.

[[cccc::build]]
int build_counter = 10;

[[cccc::build]]
int build_main(void) {
    build_counter += 32;
    return build_counter == 42 ? 42 : 1;
}
