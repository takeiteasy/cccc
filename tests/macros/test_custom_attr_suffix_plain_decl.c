// A custom attribute after a plain declarator reaches only that declarator.
#include <string.h>

int marked_count;

@comptime(attribute("mark")) void define_mark(AttrTarget *target) {
    if (strcmp(AttrTargetName(target), "x") &&
        strcmp(AttrTargetName(target), "w"))
        MacroErrorAt(0, "mark leaked onto '%s'", AttrTargetName(target));
}

int x @mark, y;
int z;
int w __attribute__((mark));

int main(void) {
    return x + y + z + w + 42;
}
