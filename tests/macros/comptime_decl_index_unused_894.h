// #894 fixture: a header full of declarations a comptime body never
// references by name. None of these should ever be spliced -- the
// demand-driven index only resolves a name on an actual lookup miss, so an
// unused declaration costs nothing and is never even parsed into the
// comptime program.
typedef struct Unused894 {
    int a;
    int b;
    int c;
} Unused894;

enum UnusedEnum894 { UE_A894, UE_B894, UE_C894 };

int unused_proto_894(int x, int y);

// Deliberately references a not-yet-declared symbol in its initializer --
// legal C (the symbol is declared below), but would need real evaluation
// to forward correctly. Since nothing ever references "unused_dependent_894"
// from comptime code, this must never be examined at all.
static int  unused_dependent_894 = 0;
static int *unused_ptr_894       = &unused_dependent_894;

// The one declaration the test actually uses.
typedef int UsedOnly894;
