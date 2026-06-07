// Ticket #307: static const pointer arrays inside comptime functions.

#include <string.h>

struct entry {
    const char *header;
    const char *fn;
};

[[jcc::comptime]]
const char *lookup_reg_fn(const char *header) {
    static const struct entry map[] = {
        {"ctype.h", "register_ctype"},
        {"stdio.h", "register_stdio"},
        {"string.h", "register_string"},
    };

    int lo = 0;
    int hi = (int)(sizeof(map) / sizeof(map[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(header, map[mid].header);
        if (cmp == 0)
            return map[mid].fn;
        if (cmp < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return 0;
}

[[jcc::comptime(inline)]]
$node_t *reg_lookup_ok(void) {
    static const char *headers[] = {"ctype.h", "stdio.h", "string.h"};

    if (strcmp(headers[1], "stdio.h") != 0)
        return $int_literal(1);
    if (strcmp(lookup_reg_fn(headers[2]), "register_string") != 0)
        return $int_literal(2);
    if (lookup_reg_fn("missing.h") != 0)
        return $int_literal(3);
    return $int_literal(42);
}

int main(void) {
    return reg_lookup_ok();
}
