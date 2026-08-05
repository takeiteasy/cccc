// #894 fixture: a decl-only header (no comptime code of its own) declaring
// a plain, uninitialized object. Types/tags/prototypes/enum constants
// splice freely from a header like this, but a plain OBJECT must not --
// #890/#893's rule (primary file, or a comptime-defining file) still
// applies, since init_macro_globals only allocates data-segment storage
// once, before any demand-driven splice could add this one in.
static int outside_global_894;
