/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// The attribute registry: one table of every attribute cccc knows by name,
// read by the attribute parser, the preprocessor's structural markers and
// include routes, __has_attribute, __has_c_attribute, the `@name` rewrite and
// the preprocessed-output re-spelling.

#include "./internal.h"

// `apply` is the parse-time handler; `marker` and `route` name the
// preprocessor's structural uses. A row with none of them is recognized and
// ignored.
#define CCCC_ROW(n, ...)     {n, ATTR_CCCC, true, 1, __VA_ARGS__}
#define STD_ROW(n, has, ...) {n, ATTR_STD, has, 202311L, __VA_ARGS__}
#define GNU_ROW(n, ...)      {n, ATTR_GNU, true, 0, __VA_ARGS__}

static const AttrDef attr_table[] = {
    // CCCC-specific (cccc:: scoped); vendor attributes report 1, not a date
    CCCC_ROW("comptime", ATTR_KEYWORD_ALIAS, NULL, ATTR_MARK_COMPTIME,
             INCLUDE_ROUTE_COMPTIME),
    CCCC_ROW("emit", 0, NULL, ATTR_MARK_NONE, INCLUDE_ROUTE_EMIT),
    CCCC_ROW("shared", 0, NULL, ATTR_MARK_NONE, INCLUDE_ROUTE_SHARED),
    CCCC_ROW("build", 0, NULL, ATTR_MARK_BUILD, INCLUDE_ROUTE_BUILD),
    CCCC_ROW("build_target", 0, NULL, ATTR_MARK_BUILD_TARGET),
    CCCC_ROW("optimize", ATTR_OUT_GNU, attr_optimize),
    CCCC_ROW("test", ATTR_KEYWORD_ALIAS, NULL, ATTR_MARK_TEST,
             INCLUDE_ROUTE_TEST),
    CCCC_ROW("test_setup", ATTR_KEYWORD_ALIAS, NULL, ATTR_MARK_TEST_SETUP),
    CCCC_ROW("test_teardown", ATTR_KEYWORD_ALIAS, NULL,
             ATTR_MARK_TEST_TEARDOWN),
    // Checked-pointer qualifiers, valid after a `*`; bounds casts, valid only
    // in a cast
    CCCC_ROW("single", 0, attr_checked_ptr),
    CCCC_ROW("array", 0, attr_checked_ptr),
    CCCC_ROW("ntarray", 0, attr_checked_ptr),
    CCCC_ROW("count", 0, attr_checked_ptr),
    CCCC_ROW("byte_count", 0, attr_checked_ptr),
    CCCC_ROW("bounds", 0, attr_checked_ptr),
    CCCC_ROW("assume", 0, attr_checked_ptr),
    CCCC_ROW("dynamic", 0, attr_checked_ptr),
    // Checked regions: a declaration or a compound statement
    CCCC_ROW("checked", 0, attr_checked),
    CCCC_ROW("unchecked", 0, attr_unchecked),
    CCCC_ROW("trusted", 0, attr_trusted),
    CCCC_ROW("kernel", 0, attr_kernel),
    CCCC_ROW("operator", 0, attr_operator),
    CCCC_ROW("method", 0, attr_method),
    // OpenCL address spaces: the bare names are common identifiers, so they
    // apply only under `cccc::`.
    CCCC_ROW("global", ATTR_NS_CCCC, attr_addr_space),
    CCCC_ROW("local", ATTR_NS_CCCC, attr_addr_space),
    CCCC_ROW("constant", ATTR_NS_CCCC, attr_addr_space),
    CCCC_ROW("private", ATTR_NS_CCCC, attr_addr_space),
    CCCC_ROW("generic", ATTR_NS_CCCC, attr_addr_space),
    {"cccc_visible_lanes", ATTR_CCCC, false, 0, 0, attr_visible_lanes},
    // Standard C23 ([[name]]); the date is N3220 section 6.10.10.2's
    STD_ROW("maybe_unused", false, 0, attr_unused),
    STD_ROW("deprecated", true, 0, attr_deprecated),
    STD_ROW("noreturn", true, 0, attr_noreturn),
    STD_ROW("nodiscard", false, 0, attr_nodiscard),
    // fallthrough also has a GNU `__attribute__` form, so __has_attribute
    // reports it
    STD_ROW("fallthrough", true, 0, attr_fallthrough),
    STD_ROW("no_unique_address", false, 0, attr_no_unique_address),
    // GNU (`__attribute__((name))`)
    GNU_ROW("aligned", 0, attr_aligned),
    GNU_ROW("packed", 0, attr_packed),
    GNU_ROW("unused", 0, attr_unused),
    GNU_ROW("__unused__", 0, attr_unused),
    GNU_ROW("__deprecated__", 0, attr_deprecated),
    GNU_ROW("format", 0, attr_format),
    GNU_ROW("cleanup", 0, attr_cleanup),
    GNU_ROW("error", 0, attr_error),
    GNU_ROW("warning", 0, attr_warning),
    GNU_ROW("nonnull", 0, attr_nonnull),
    GNU_ROW("returns_nonnull", 0, attr_returns_nonnull),
    GNU_ROW("pure", 0, attr_pure),
    GNU_ROW("const", 0, attr_const),
    GNU_ROW("warn_unused_result", 0, attr_warn_unused_result),
    GNU_ROW("sentinel", 0, attr_sentinel),
    GNU_ROW("alloc_size", 0, attr_alloc_size),
    GNU_ROW("malloc", 0, attr_malloc),
    GNU_ROW("vector_size", 0, attr_vector_size),
    GNU_ROW("designated_init", 0, attr_designated_init),
    GNU_ROW("gnu_inline", 0, attr_gnu_inline),
    GNU_ROW("constructor", 0, attr_constructor),
    GNU_ROW("destructor", 0, attr_destructor),
    // Recognized and ignored: there is no linker, per-function ISA, inliner,
    // branch layout, symbol-level DCE, strict-aliasing optimizer or machine
    // mode.
    GNU_ROW("visibility", 0),
    GNU_ROW("section", 0),
    GNU_ROW("weak", 0),
    GNU_ROW("alias", 0),
    GNU_ROW("target", 0),
    GNU_ROW("always_inline", 0),
    GNU_ROW("noinline", 0),
    GNU_ROW("flatten", 0),
    GNU_ROW("cold", 0),
    GNU_ROW("hot", 0),
    GNU_ROW("used", 0),
    GNU_ROW("may_alias", 0),
    GNU_ROW("mode", 0),
    GNU_ROW("transparent_union", 0),
    {NULL},
};

// Attribute names handled by derive.h's @comptime(attribute(...)) handlers.
// Used to hint at the missing #include when one is used without it, and by
// the attribute registry (__has_c_attribute, @name routing). Keep in sync with
// that header (tests/host/test_derive_attr_list.c checks it).
static const char *const derive_attr_names[] = {
    "serialize",      "deserialize",  "to_string", "from_string",
    "to_string_skip", "getters",      "setters",   "accessors",
    "ctor",           "default",      "to_json",   "from_json",
    "json_skip",      "json_name",    "equals",    "hash",
    "dump",           "equals_skip",  "dump_skip", "serialize_skip",
    "accessor_skip",  "ctor_skip",    "bitflags",  "compare",
    "compare_order",  "compare_desc", "range",     "validate",
    "clamp",          "owned",        "clone",     "destroy",
    "json",           "text",         "binary",    "lifecycle",
    "hashable",       "ordered",      NULL,
};

const char *const *derive_builtin_attr_names(void) {
    return derive_attr_names;
}

bool is_derive_builtin_attr(const char *name, int len) {
    for (int i = 0; derive_attr_names[i]; i++)
        if ((int)strlen(derive_attr_names[i]) == len &&
            !strncmp(derive_attr_names[i], name, len))
            return true;
    return false;
}

static const AttrDef *lookup_exact(const char *name, size_t len) {
    for (int i = 0; attr_table[i].name; i++)
        if (attr_table[i].name[0] == name[0] &&
            strlen(attr_table[i].name) == len &&
            !strncmp(attr_table[i].name, name, len))
            return &attr_table[i];

    static const AttrDef derive_def = {"", ATTR_CCCC, true, 1, 0};
    if (is_derive_builtin_attr(name, (int)len))
        return &derive_def;
    return NULL;
}

const AttrDef *lookup_attr(const char *name, size_t len) {
    const AttrDef *exact = lookup_exact(name, len);
    if (exact)
        return exact;

    // GNU `__x__` spelling: the explicit `__unused__`/`__deprecated__` rows
    // above still win via the exact match.
    if (len >= 5 && name[0] == '_' && name[1] == '_' && name[len - 1] == '_' &&
        name[len - 2] == '_')
        return lookup_exact(name + 2, len - 4);
    return NULL;
}

bool cc_has_attribute(const char *name, int len) {
    const AttrDef *a = len > 0 ? lookup_attr(name, (size_t)len) : NULL;
    return a && a->has_attr;
}

// The category whose spelling preprocessed output uses for `name`, or -1 when
// the name is unknown.
int attr_output_home(const char *name, size_t len) {
    if (len == 5 && !strncmp(name, "macro", 5))
        return ATTR_CCCC;
    const AttrDef *a = lookup_attr(name, len);
    if (!a)
        return -1;
    return (a->flags & ATTR_OUT_GNU) ? ATTR_GNU : (int)a->cat;
}
