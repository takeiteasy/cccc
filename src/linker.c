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

#include "./internal.h"

static Token *must_tokenize_file(VirtualMachine *vm, char *path) {
    Token *tok = tokenize_file(vm, path, true);
    if (!tok)
        error("%s: %s", path, strerror(errno));
    return tok;
}

static Token *append_tokens(Token *tok1, Token *tok2) {
    if (!tok1 || tok1->kind == TK_EOF)
        return tok2;

    Token *t = tok1;
    while (t->next->kind != TK_EOF)
        t = t->next;
    t->next = tok2;
    return tok1;
}

Token *cc_preprocess(VirtualMachine *vm, const char *path) {
    Token *tok = NULL;

    // Process -include option
    // for (int i = 0; i < opt_include.len; i++) {
    //     char *incl = opt_include.data[i];

    //     char *file_path;
    //     if (file_exists(incl))
    //         file_path = incl;
    //     else {
    //         file_path = search_include_paths(vm, incl);
    //         if (!file_path)
    //             error("-include: %s: %s", incl, strerror(errno));
    //     }

    //     Token *tok2 = must_tokenize_file(vm, file_path);
    //     tok = append_tokens(tok, tok2);
    // }

    // Tokenize and parse.
    Token *tok2 = must_tokenize_file(vm, (char *)path);
    if (!vm->compiler.primary_file && tok2)
        vm->compiler.primary_file = tok2->file;
    tok = append_tokens(tok, tok2);
    if (!vm->compiler.skip_preprocess) {
        tok = preprocess(vm, tok);
    }

    return tok;
}

Obj *cc_parse(VirtualMachine *vm, Token *tok) {
    return parse(vm, tok);
}

void cc_print_tokens(Token *tok) {
    FILE *out  = stdout;

    int   line = 1;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (line > 1 && tok->at_bol)
            fprintf(out, "\n");
        if (tok->has_space && !tok->at_bol)
            fprintf(out, " ");
        fprintf(out, "%.*s", tok->len, tok->loc);
        line++;
    }
    fprintf(out, "\n");
}

Obj *cc_link_progs(VirtualMachine *vm, Obj **progs, int count) {
    if (!vm || !progs || count <= 0)
        error("cc_link_progs: invalid arguments");
    if (count == 1)
        return progs[0];

    // Store progs for later offset propagation
    vm->compiler.link_prog_count = count;
    vm->compiler.link_progs      = malloc(sizeof(Obj *) * count);
    for (int i = 0; i < count; i++) {
        vm->compiler.link_progs[i] = progs[i];
    }

    // #957: cc_link_progs runs twice on the --ffi-decls path (main.c calls
    // it once to gather FFI signatures, then again for the real build) --
    // reset the alias count (not free: the array itself is reused/grown by
    // realloc below) so a second run doesn't append onto stale entries from
    // the first.
    vm->compiler.global_aliases_count = 0;

    // Build a hashmap to detect duplicate external-linkage symbols.
    // Internal-linkage objects are file-local and must not be canonicalized by
    // name across translation units.
    HashMap symbol_map = {0};
    // First pass: collect all symbols, preferring definitions
    for (int i = 0; i < count; i++) {
        for (Obj *obj = progs[i]; obj; obj = obj->next) {
            if (obj->is_static)
                continue;

            Obj *existing   = hashmap_get(&symbol_map, obj->name);

            bool obj_is_def = obj->is_definition ||
                              (obj->is_function && obj->body) ||
                              (!obj->is_function && obj->init_data);

            if (!existing) {
                // New symbol, add it
                hashmap_put(&symbol_map, obj->name, obj);
            } else {
                // Symbol already exists - handle conflicts
                bool existing_is_def =
                    existing->is_definition ||
                    (existing->is_function && existing->body) ||
                    (!existing->is_function && existing->init_data);

                if (obj_is_def && existing_is_def) {
                    // Both are definitions - error
                    error_tok(vm, obj->tok, "redefinition of '%s'", obj->name);
                } else if (obj_is_def) {
                    // New one is definition, replace declaration
                    hashmap_put(&symbol_map, obj->name, obj);
                    // Copy definition's properties to the declaration for AST
                    // node references
                    existing->is_definition = obj->is_definition;
                    existing->init_data     = obj->init_data;
                    existing->rel           = obj->rel;
                    existing->ty            = obj->ty;
                } else if (existing_is_def) {
                    // Existing is definition, copy its properties to this
                    // declaration
                    obj->is_definition = existing->is_definition;
                    obj->init_data     = existing->init_data;
                    obj->rel           = existing->rel;
                    obj->ty            = existing->ty;
                }
                // Otherwise both are declarations, keep first one
            }
        }
    }

    // Second pass: build the merged linked list and propagate definition info
    Obj *merged = NULL;
    Obj *tail   = NULL;
    for (int i = 0; i < count; i++) {
        for (Obj *obj = progs[i]; obj;) {
            // Save next pointer before potentially modifying obj
            Obj *next_obj = obj->next;

            Obj *canonical =
                obj->is_static ? obj : hashmap_get(&symbol_map, obj->name);

            // If this is not the canonical version, update it to reference the
            // canonical one
            if (canonical && canonical != obj) {
                // This is a declaration - copy properties from the definition
                // Note: offset will be set during codegen for the canonical
                // object For now, we mark this object to point to the canonical
                // one
                obj->is_definition = canonical->is_definition;
                obj->init_data     = canonical->init_data;
                obj->rel           = canonical->rel;
                obj->ty            = canonical->ty;

                // #957: this non-canonical Obj is dropped from the merged
                // list below and never reaches gen()'s data-segment
                // allocation loop, so it would otherwise keep whatever
                // offset it had at creation (0, since codegen never ran) --
                // any reference compiled against it (gen_addr bakes
                // var->offset in as an immediate) would silently read/write
                // data-segment offset 0 instead of the canonical global's
                // real slot. Record the pair here; gen() copies
                // canonical->offset onto every alias right after the
                // allocation loop runs. Functions are called by name
                // (CALL patched by call_patches), not by baked-in offset,
                // so they need no such propagation.
                if (!obj->is_function) {
                    if (vm->compiler.global_aliases_count ==
                        vm->compiler.global_aliases_cap) {
                        vm->compiler.global_aliases_cap =
                            vm->compiler.global_aliases_cap
                                ? vm->compiler.global_aliases_cap * 2
                                : 8;
                        vm->compiler.global_aliases =
                            realloc(vm->compiler.global_aliases,
                                    sizeof(*vm->compiler.global_aliases) *
                                        vm->compiler.global_aliases_cap);
                    }
                    vm->compiler
                        .global_aliases[vm->compiler.global_aliases_count]
                        .alias = obj;
                    vm->compiler
                        .global_aliases[vm->compiler.global_aliases_count]
                        .canonical = canonical;
                    vm->compiler.global_aliases_count++;
                }

                // #1233: unlike the offset propagation above, is_used/
                // is_maybe_unused/is_deprecated need to reach the canonical
                // Obj for *every* alias, function or not -- each TU parses
                // its own copy of a shared prototype (e.g. a bundled
                // header's `extern FILE *__cccc_stderr(void);`, reached
                // through every TU that includes <stdio.h>) as its own
                // distinct Obj, and only the alias actually called within
                // that TU's own body has is_used set. A function alias used
                // to fall outside this block entirely (scoped to
                // `!obj->is_function` purely for the offset-propagation
                // reason above, which has nothing to do with is_used) --
                // the canonical Obj kept whatever is_used it started with,
                // silently losing every other TU's usage. This is what let
                // -c=native's own accessor-shim gating (native_accessor_
                // shims, src/serialize_shims.c, matched by checking
                // obj->is_used on the merged program's canonical Obj) skip
                // emitting `__cccc_stdout`/`__cccc_stderr` whenever the only
                // call site lived in a non-primary TU -- "use of undeclared
                // identifier '__cccc_stderr'" from the host compiler with no
                // cccc-side diagnostic at all.
                canonical->is_used         |= obj->is_used;
                canonical->is_maybe_unused |= obj->is_maybe_unused;
                canonical->is_deprecated   |= obj->is_deprecated;
            }

            // Only add canonical objects to the merged list
            if (canonical == obj) {
                // Clear the next pointer to avoid dangling references
                obj->next = NULL;

                if (!merged) {
                    merged = obj;
                    tail   = obj;
                } else {
                    tail->next = obj;
                    tail       = obj;
                }
            }

            obj = next_obj; // Move to next using saved pointer
        }
    }

    hashmap_deinit(&symbol_map);
    return merged;
}
