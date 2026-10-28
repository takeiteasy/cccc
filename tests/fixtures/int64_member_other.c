#include "int64_member.h"

struct Int64Pair int64_pair_make(int64_t a, uint64_t b) {
    return (struct Int64Pair){a, b};
}
