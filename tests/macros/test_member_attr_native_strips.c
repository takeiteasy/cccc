// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct Rec
// CCCC_REJECT_STDOUT: json_skip
// CCCC_REJECT_STDOUT: json_name
// CCCC_REJECT_STDOUT: __attribute__
#include <cccc/derive.h>

struct Rec {
    int id @json_name("rec_id");
    int hidden @json_skip;
};

int main(void) {
    struct Rec r = {40, 2};
    return r.id + r.hidden;
}
