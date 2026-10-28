// A pointer to an anonymous struct declared in a header, indexed and assigned
// element to element, stays one type under -c=native.
#include <stdlib.h>
#include "fixtures/anon_member_ptr.h"

int main(void) {
    struct Patches p    = {0};
    p.items             = calloc(4, sizeof *p.items);
    p.items[1].location = 40;
    p.items[2].location = 2;
    int kept            = 0;
    for (int i = 0; i < 3; i++)
        if (p.items[i].location)
            p.items[kept++] = p.items[i];
    int r = p.items[0].location + p.items[1].location;
    free(p.items);
    return kept == 2 ? r : 1;
}
