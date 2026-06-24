// CCCC_FLAGS: --build
// [[cccc::build]] on an enum is attribute-stripped: the enum is compiled
// and accessible in build mode.
// Note: 'BuildTarget' is a reserved name in building.h - use a distinct name.

[[cccc::build]]
enum MyBuildKind {
    KIND_NATIVE = 0,
    KIND_BYTECODE = 1,
};

[[cccc::build]]
int build_main(void) {
    enum MyBuildKind k = KIND_BYTECODE;
    return k == 1 ? 42 : 1;
}
