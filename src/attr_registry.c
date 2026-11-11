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
// read by __has_attribute, __has_c_attribute, the `@name` rewrite and the
// preprocessed-output re-spelling.

#include "./internal.h"

static const AttrDef attr_table[] = {
    // CCCC-specific (cccc:: scoped) — vendor attrs report 1, not a date
    {"comptime", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    {"emit", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    {"optimize", ATTR_CCCC, true, 1, 0},
    {"test", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    {"test_setup", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    {"test_teardown", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    // Checked C-style checked-pointer attributes (#770/#482-484). Only
    // meaningful in post-'*' qualifier position (see pointers() in parse.c);
    // listed here so @single/@array/... route to [[cccc::...]] instead of
    // __attribute__((...)) and __has_c_attribute(cccc::array) etc. work.
    {"single", ATTR_CCCC, true, 1, 0},
    {"array", ATTR_CCCC, true, 1, 0},
    {"ntarray", ATTR_CCCC, true, 1, 0},
    {"count", ATTR_CCCC, true, 1, 0},
    {"byte_count", ATTR_CCCC, true, 1, 0},
    {"bounds", ATTR_CCCC, true, 1, 0},
    // Checked-pointer bounds-cast attributes (#486): [[cccc::assume]] /
    // [[cccc::dynamic]], cast-only (see declarator()'s post-pointers()
    // check, src/parse_types.c). Listed here for the same @name-routing
    // reason as the six above.
    {"assume", ATTR_CCCC, true, 1, 0},
    {"dynamic", ATTR_CCCC, true, 1, 0},
    // Checked-region attributes (#485). Unlike the six above these
    // appertain to a declaration or a compound statement, not to a TY_PTR
    // (see apply_checked_scope_attr(), src/parse_types.c); listed here so
    // @checked/@unchecked route to [[cccc::...]] the same way.
    {"checked", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    {"unchecked", ATTR_CCCC, true, 1, ATTR_OUT_CCCC},
    {"trusted", ATTR_CCCC, true, 1, 0},
    {"kernel", ATTR_CCCC, true, 1, 0},
    {"global", ATTR_CCCC, true, 1, 0},
    {"local", ATTR_CCCC, true, 1, 0},
    {"constant", ATTR_CCCC, true, 1, 0},
    {"private", ATTR_CCCC, true, 1, 0},
    {"generic", ATTR_CCCC, true, 1, 0},
    // Standard C23 ([[name]]) — return C23 version date per N3220 §6.10.10.2
    {"maybe_unused", ATTR_STD, false, 202311L, 0},
    {"deprecated", ATTR_STD, true, 202311L, 0},
    {"noreturn", ATTR_STD, true, 202311L, 0},
    {"nodiscard", ATTR_STD, false, 202311L, 0},
    // fallthrough has a GNU __attribute__ form with real compiler semantics
    // (matches GCC/Clang __has_attribute), so has_attr=true here even though
    // it's listed under the C23 std table for __has_c_attribute's date value.
    {"fallthrough", ATTR_STD, true, 202311L, 0},
    {"no_unique_address", ATTR_STD, false, 202311L, 0},
    // GNU-only (__attribute__((name)))
    {"aligned", ATTR_GNU, true, 0, 0},
    {"packed", ATTR_GNU, true, 0, 0},
    {"unused", ATTR_GNU, true, 0, 0},
    {"__unused__", ATTR_GNU, true, 0, 0},
    {"__deprecated__", ATTR_GNU, true, 0, 0},
    {"format", ATTR_GNU, true, 0, 0},
    // Genuinely-implemented GNU attributes with real compiler semantics
    // (ticket #681) — these were missing from this table despite being
    // fully handled in src/parse.c, so __has_attribute wrongly reported 0.
    {"cleanup", ATTR_GNU, true, 0, 0},
    {"error", ATTR_GNU, true, 0, 0},
    {"warning", ATTR_GNU, true, 0, 0},
    {"nonnull", ATTR_GNU, true, 0, 0},
    {"returns_nonnull", ATTR_GNU, true, 0, 0},
    {"pure", ATTR_GNU, true, 0, 0},
    {"const", ATTR_GNU, true, 0, 0},
    {"warn_unused_result", ATTR_GNU, true, 0, 0},
    // Recognized but architecturally inert (ticket #657): no ELF/Mach-O
    // output, no linker, no per-function ISA codegen, no inliner, no
    // branch-temperature layout, no symbol-level DCE, no strict-aliasing
    // optimizer, no machine-mode type system. Accepted silently by the
    // attribute parsers' generic fallback (src/parse_types.c) and reported
    // as recognized so __has_attribute matches real GCC/Clang.
    {"visibility", ATTR_GNU, true, 0, 0},
    {"section", ATTR_GNU, true, 0, 0},
    {"weak", ATTR_GNU, true, 0, 0},
    {"alias", ATTR_GNU, true, 0, 0},
    {"target", ATTR_GNU, true, 0, 0},
    {"always_inline", ATTR_GNU, true, 0, 0},
    {"gnu_inline", ATTR_GNU, true, 0, 0},
    {"noinline", ATTR_GNU, true, 0, 0},
    {"flatten", ATTR_GNU, true, 0, 0},
    {"cold", ATTR_GNU, true, 0, 0},
    {"hot", ATTR_GNU, true, 0, 0},
    {"used", ATTR_GNU, true, 0, 0},
    {"may_alias", ATTR_GNU, true, 0, 0},
    {"mode", ATTR_GNU, true, 0, 0},
    {"transparent_union", ATTR_GNU, true, 0, 0},
    {"constructor", ATTR_GNU, true, 0, 0},
    {"destructor", ATTR_GNU, true, 0, 0},
    {NULL, 0, false, 0, 0},
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
        if (strlen(attr_table[i].name) == len &&
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

bool attr_output_cccc(const char *name) {
    const AttrDef *a = lookup_exact(name, strlen(name));
    return (a && (a->flags & ATTR_OUT_CCCC)) || !strcmp(name, "macro");
}

bool attr_output_std(const char *name) {
    const AttrDef *a = lookup_exact(name, strlen(name));
    return a && a->cat == ATTR_STD;
}
