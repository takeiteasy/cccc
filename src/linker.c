/*
 JCC: JIT C Compiler

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

#include "jcc.h"
#include "./internal.h"

static Token *must_tokenize_file(JCC *vm, char *path) {
    Token *tok = tokenize_file(vm, path);
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

Token *cc_preprocess(JCC *vm, const char *path) {
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
    Token *tok2 = must_tokenize_file(vm, (char*)path);
    tok = append_tokens(tok, tok2);
    if (!vm->compiler.skip_preprocess) {
        tok = preprocess(vm, tok);
    }

    return tok;
}

Obj *cc_parse(JCC *vm, Token *tok) {
    return parse(vm, tok);
}

void cc_print_tokens(Token *tok) {
    FILE *out = stdout;

    int line = 1;
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

Obj *cc_link_progs(JCC *vm, Obj **progs, int count) {
    if (!vm || !progs || count <= 0)
        error("cc_link_progs: invalid arguments");
    if (count == 1)
        return progs[0];
    
    // Store progs for later offset propagation
    vm->compiler.link_prog_count = count;
    vm->compiler.link_progs = malloc(sizeof(Obj*) * count);
    for (int i = 0; i < count; i++) {
        vm->compiler.link_progs[i] = progs[i];
    }
    
    // Build a hashmap to detect duplicate symbols
    // We'll prefer definitions over declarations
    HashMap symbol_map = {0};
    // First pass: collect all symbols, preferring definitions
    for (int i = 0; i < count; i++) {
        for (Obj *obj = progs[i]; obj; obj = obj->next) {
            Obj *existing = hashmap_get(&symbol_map, obj->name);
            
            bool obj_is_def = obj->is_definition || 
                             (obj->is_function && obj->body) ||
                             (!obj->is_function && obj->init_data);
            
            if (!existing) {
                // New symbol, add it
                hashmap_put(&symbol_map, obj->name, obj);
            } else {
                // Symbol already exists - handle conflicts
                bool existing_is_def = existing->is_definition ||
                                      (existing->is_function && existing->body) ||
                                      (!existing->is_function && existing->init_data);
                
                if (obj_is_def && existing_is_def) {
                    // Both are definitions - error
                    error_tok(vm, obj->tok, "redefinition of '%s'", obj->name);
                } else if (obj_is_def) {
                    // New one is definition, replace declaration
                    hashmap_put(&symbol_map, obj->name, obj);
                    // Copy definition's properties to the declaration for AST node references
                    existing->is_definition = obj->is_definition;
                    existing->init_data = obj->init_data;
                    existing->rel = obj->rel;
                    existing->ty = obj->ty;
                } else if (existing_is_def) {
                    // Existing is definition, copy its properties to this declaration
                    obj->is_definition = existing->is_definition;
                    obj->init_data = existing->init_data;
                    obj->rel = existing->rel;
                    obj->ty = existing->ty;
                }
                // Otherwise both are declarations, keep first one
            }
        }
    }
    
    // Second pass: build the merged linked list and propagate definition info
    Obj *merged = NULL;
    Obj *tail = NULL;
    for (int i = 0; i < count; i++) {
        for (Obj *obj = progs[i]; obj;) {
            // Save next pointer before potentially modifying obj
            Obj *next_obj = obj->next;
            
            Obj *canonical = hashmap_get(&symbol_map, obj->name);
            
            // If this is not the canonical version, update it to reference the canonical one
            if (canonical && canonical != obj) {
                // This is a declaration - copy properties from the definition
                // Note: offset will be set during codegen for the canonical object
                // For now, we mark this object to point to the canonical one
                obj->is_definition = canonical->is_definition;
                obj->init_data = canonical->init_data;
                obj->rel = canonical->rel;
                obj->ty = canonical->ty;
            }
            
            // Only add canonical objects to the merged list
            if (canonical == obj) {
                // Clear the next pointer to avoid dangling references
                obj->next = NULL;
                
                if (!merged) {
                    merged = obj;
                    tail = obj;
                } else {
                    tail->next = obj;
                    tail = obj;
                }
            }
            
            obj = next_obj;  // Move to next using saved pointer
        }
    }
    
    return merged;
}
