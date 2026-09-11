// #1272: an ordinary file that happens to define functions named Build and
// Executable -- two of building.h's own macro-wrapped names -- but carries
// no @test/@build/@build_target attribute anywhere and passes no
// --testing/--build flag. The attribute-position scan must not fire on a
// bare identifier in ordinary code position, or these definitions would
// collide with the injected building.h declarations and fail to compile.
// Regression guard for the demand-driven injection staying demand-driven.

int Build(void) {
    return 1;
}
int Executable(void) {
    return 2;
}

int main(void) {
    return (Build() + Executable() == 3) ? 42 : 1;
}
