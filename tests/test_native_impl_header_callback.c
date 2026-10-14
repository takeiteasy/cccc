// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: static void on_cb\(int v\);\nstatic int g_hits;\nstatic void note\(int v\);\n#include "native_impl_header_callback.h"
// -m/-c=native: a replayed implementation-style header that calls the
// includer's static function, takes its address and bumps its static counter
// through config macros needs those declarations ahead of the #include.
#include <stdlib.h>

static int g_seen;
static int g_hits;

static void note(int v) { g_seen += v; }
static void on_cb(int v) { g_seen += v; }

#define LIB_IMPL
#define LIB_LOG(x) note(x)
#define LIB_COUNT  g_hits
#define LIB_CB     (&on_cb)
#include "native_impl_header_callback.h"

int main(void) {
    int r = lib_run(20);
    return (r == 20 && g_seen == 40 && g_hits == 1) ? 42 : 1;
}
