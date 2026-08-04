// Ticket #890: a void-returning global-generation comptime macro that reads
// its own file's file-scope statics must work the same whether the call is
// written directly in that file or arrives via #include from a driver file.
// #include is textual, so the two cases must be indistinguishable.

#include "comptime_include_boundary_890.h"

generate_result_890();

int main(void) {
    return result_890();
}
