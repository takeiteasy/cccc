// Ticket #1048: a header reached only via a plain #include (never routed
// via `#include @comptime "x.h"`), but containing its own
// [[cccc::comptime]] declarations, used to replay verbatim into
// -c=native output -- the host compiler got past the harmlessly-ignored
// unknown-attribute warning, then hit the comptime-only body text
// (Obj/MakeFunction/GetType, reflection-API constructs with no host
// meaning) as an undeclared identifier/function.
//
// #896 already marks a file cccc-only the moment a directive inside it
// uses routing syntax (`#include @comptime "x.h"`, `@shared`, etc), so
// its own text is never replayed and its type/function definitions are
// instead re-derived by the existing from_include compensation machinery.
// There was no equivalent marking for the `[[cccc::comptime]]`/
// `__attribute__((comptime))` *attribute* form -- a header using only
// that spelling, with no routed directive anywhere, fell straight through
// to ordinary auto-capture replay.
//
// Fixed in try_extract_attr_macro() (src/preprocess.c): the containing
// file is marked cccc-only the moment a comptime declaration is
// recognized, mirroring #896's directive-level granularity but keyed off
// the attribute instead. Excludes tokenize_private_header()'s own
// synthetic tags (<implicit-reflection.h>/<building.h>/<testing.h>) and
// __builtin_quote's <quote> pseudo-file by exact match -- the same
// #1034/#892 regression a broader "<...>" prefix match hit before
// (neither is ever reached through the ordinary #include auto-capture
// path anyway, so excluding them costs nothing).
#include "test_serialize_comptime_header_1048.h"

generate_result_1048();

int main(void) {
    return result_1048();
}
