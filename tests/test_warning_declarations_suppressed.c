// clang-format off
// CCCC_FLAGS: --std=c89 -Wall -Wno-implicit-int -Wno-implicit-function-declaration -Wno-return-type -Wno-pedantic
// clang-format on
// CCCC_REJECT_STDERR: warning:
/* #1144: implicit function declaration is a hard error at C99+; this
   test's forward call to implicit_later() (below) needs --std=c89 to stay
   a (suppressed via -Wno-implicit-function-declaration) warning rather
   than fail the compile outright. -Wno-pedantic silences the file's own
   now-nonstandard-at-c89 '//' comments (including this one), which -Wall
   would otherwise also flag. */
global_value;

int zero(void) {
    return;
}

int fallthrough(void) {}

int main(void) {
    return global_value + zero() + fallthrough() + implicit_later();
}

int implicit_later(void) {
    return 42;
}
