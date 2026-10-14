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

// Reloading a source file into a running session: cc_debug_reload.
//
// The file is preprocessed and parsed into a file scope of its own, then each
// new definition is matched to the live one it replaces. A changed function
// becomes a hot-reload redefinition (Obj.reload_of); an unchanged function or
// a kept global is not compiled again (Obj.reuse_of). Every check runs before
// code is generated, and the new code is linked in one step, so a failed
// reload leaves the session as it was. Comptime code is not part of the running
// program, so a file that changes or adds a comptime function or variable,
// calls one at file scope, or calls one from a changed function is refused.

#include "internal.h"
#include "parse_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int changed, unchanged, added, removed;
} ReloadStats;

static char *file_key(const File *f) {
    char buf[PATH_MAX];
    if (!f || !f->name)
        return NULL;
    return strdup(realpath(f->name, buf) ? buf : f->name);
}

// The file a file-scope Obj was written in.
static const File *obj_file(const Obj *o) {
    if (o->def_begin && o->def_begin->file)
        return o->def_begin->file;
    return o->tok ? o->tok->file : NULL;
}

static bool obj_in_file(const Obj *o, const char *key) {
    char *k = file_key(obj_file(o));
    bool  r = k && !strcmp(k, key);
    free(k);
    return r;
}

// A file-scope function or variable, as opposed to a block-scope static or a
// compiler-made helper.
static bool is_file_scope_obj(const Obj *o) {
    return o->name && o->name[0] != '.' && !o->static_owner && !o->is_local &&
           !o->is_builtin_alloca;
}

static bool same_identity(const Obj *a, const Obj *b) {
    if (a->is_function != b->is_function || strcmp(a->name, b->name) ||
        a->is_static != b->is_static)
        return false;
    if (!a->is_static)
        return true;
    char *ka = file_key(obj_file(a));
    char *kb = file_key(obj_file(b));
    bool  r  = ka && kb && !strcmp(ka, kb);
    free(ka);
    free(kb);
    return r;
}

static bool is_defined(const Obj *o) {
    return o->is_function ? o->body != NULL : o->is_definition;
}

// The newest live definition `nw` replaces, or a declaration of the same
// variable when there is no definition.
static Obj *find_old(Obj *globals, Obj *old_head, Obj *nw) {
    Obj *decl = NULL;
    for (Obj *o = old_head; o; o = o->next) {
        if (!is_file_scope_obj(o) || !same_identity(o, nw))
            continue;
        if (is_defined(o))
            return o;
        if (!decl)
            decl = o;
    }
    (void)globals;
    return decl;
}

static bool bodies_equal(const Obj *a, const Obj *b) {
    const Token *ta = a->def_begin, *tb = b->def_begin;
    if (!ta || !tb || !a->def_end || !b->def_end)
        return false;
    for (;; ta = ta->next, tb = tb->next) {
        if (ta->kind != tb->kind || ta->len != tb->len ||
            memcmp(ta->loc, tb->loc, (size_t)ta->len))
            return false;
        if (ta == a->def_end || tb == b->def_end)
            return ta == a->def_end && tb == b->def_end;
    }
}

static void reload_fail(VirtualMachine *vm, const char *fmt, const char *a,
                        const char *b) {
    char msg[512];
    snprintf(msg, sizeof(msg), fmt, a, b ? b : "");
    fprintf(stderr, "error: reload: %s\n", msg);
    (void)vm;
}

static bool tokens_equal(const Token *a, const Token *b) {
    for (; a && b; a = a->next, b = b->next) {
        if (a->kind != b->kind || a->len != b->len ||
            memcmp(a->loc, b->loc, (size_t)a->len))
            return false;
        if (a->kind == TK_EOF)
            return true;
    }
    return false;
}

// Re-preprocessing the file registers its comptime functions and variables
// again. Drops each copy of a live one so the compiled entry stays in use, and
// refuses a changed or new one.
static bool settle_comptime(VirtualMachine *vm, MacroFn *old_fns,
                            ComptimeVar *old_vars) {
    for (MacroFn **link = &vm->compiler.macro_fns; *link != old_fns;) {
        MacroFn *nw   = *link;
        bool     same = false, known = false;
        for (MacroFn *m = old_fns; m && !same; m = m->next)
            if (!strcmp(m->name, nw->name)) {
                known = true;
                same  = tokens_equal(m->body_tokens, nw->body_tokens);
            }
        if (!same) {
            reload_fail(vm,
                        known ? "comptime function '%s' changed; restart "
                                "to apply it"
                              : "comptime function '%s' is new; restart "
                                "to apply it",
                        nw->name, NULL);
            return false;
        }
        *link = nw->next;
    }
    for (ComptimeVar **link = &vm->compiler.comptime_vars; *link != old_vars;) {
        ComptimeVar *nw   = *link;
        bool         same = false, known = false;
        for (ComptimeVar *c = old_vars; c && !same; c = c->next)
            if (!strcmp(c->name, nw->name)) {
                known = true;
                same  = tokens_equal(c->decl_tokens, nw->decl_tokens);
            }
        if (!same) {
            reload_fail(vm,
                        known ? "comptime variable '%s' changed; restart "
                                "to apply it"
                              : "comptime variable '%s' is new; restart "
                                "to apply it",
                        nw->name, NULL);
            return false;
        }
        *link = nw->next;
    }
    return true;
}

// Prints the diagnostics collected since `tail` and drops them from the list.
static void flush_errors(VirtualMachine *vm, CompileError *tail, int count) {
    CompileError *first = tail ? tail->next : vm->errors;
    for (CompileError *e = first; e; e = e->next) {
        fputs(e->message, stderr);
        for (CompileError *n = e->notes; n; n = n->next)
            fputs(n->message, stderr);
    }
    if (tail)
        tail->next = NULL;
    else
        vm->errors = NULL;
    vm->errors_tail = tail;
    vm->error_count = count;
}

// Names the first tag, typedef or enumerator the new file scope defines
// differently from the live one, or returns NULL.
static const char *changed_declaration(Scope *live, Scope *fresh,
                                       const char **kind) {
    for (TagScopeNode *t = fresh->tags; t; t = t->next) {
        for (Scope *sc = live; sc; sc = sc->next)
            for (TagScopeNode *o = sc->tags; o; o = o->next)
                if (o->name_len == t->name_len &&
                    !strncmp(o->name, t->name, (size_t)t->name_len)) {
                    if (!types_same_layout(o->ty, t->ty)) {
                        *kind = "type";
                        return strndup(t->name, (size_t)t->name_len);
                    }
                    goto next_tag;
                }
    next_tag:;
    }
    for (VarScopeNode *v = fresh->vars; v; v = v->next) {
        if (!v->type_def && !v->enum_ty)
            continue;
        for (Scope *sc = live; sc; sc = sc->next) {
            VarScope *o = scope_find_var(sc, v->name, v->name_len);
            if (!o)
                continue;
            bool differs =
                (v->type_def && o->type_def &&
                 !types_same_layout(o->type_def, v->type_def)) ||
                (v->enum_ty && o->enum_ty && o->enum_val != v->enum_val);
            if (differs) {
                *kind = v->type_def ? "typedef" : "enumerator";
                return strndup(v->name, (size_t)v->name_len);
            }
            break;
        }
    }
    return NULL;
}

// Pairs every new file-scope Obj with the live one it replaces. Returns false
// after printing why the reload is refused.
static bool match_objects(VirtualMachine *vm, Obj *old_head, ReloadStats *st) {
    for (Obj *nw = vm->compiler.globals; nw != old_head; nw = nw->next) {
        if (!is_file_scope_obj(nw))
            continue;
        Obj *old = find_old(vm->compiler.globals, old_head, nw);

        if (nw->is_function) {
            if (!nw->body)
                continue;
            if (!old || !old->body) {
                st->added++;
                continue;
            }
            if (!types_same_layout(old->ty, nw->ty) ||
                old->is_inline != nw->is_inline) {
                reload_fail(vm, "'%s' changes its signature", nw->name, NULL);
                return false;
            }
            if (bodies_equal(old, nw)) {
                nw->reuse_of = old;
                st->unchanged++;
            } else {
                nw->reload_of = reuse_root(old);
                st->changed++;
            }
            continue;
        }

        if (!old) {
            if (nw->is_definition)
                st->added++;
            continue;
        }
        if (nw->is_definition && old->is_definition) {
            if (!types_same_layout(old->ty, nw->ty)) {
                reload_fail(vm, "global '%s' changes its type", nw->name, NULL);
                return false;
            }
            if (nw->is_constexpr &&
                (!old->init_data || !nw->init_data ||
                 memcmp(old->init_data, nw->init_data, (size_t)nw->ty->size))) {
                reload_fail(vm, "constexpr '%s' changes its value", nw->name,
                            NULL);
                return false;
            }
        }
        if (old->is_definition || !nw->is_definition)
            nw->reuse_of = old;
    }
    return true;
}

// Refuses a new or changed definition that calls a comptime function: there is
// no compiled comptime code to expand it with.
static bool no_comptime_calls(VirtualMachine *vm, Obj *old_head) {
    for (Obj *nw = vm->compiler.globals; nw != old_head; nw = nw->next) {
        if (!is_file_scope_obj(nw) || nw->reuse_of)
            continue;
        if (nw->has_pending_macro_init ||
            (nw->is_function && nw->body &&
             cc_find_comptime_call(vm, nw->def_begin, nw->def_end))) {
            reload_fail(vm,
                        "'%s' calls a comptime function; restart to apply "
                        "the change",
                        nw->name, NULL);
            return false;
        }
    }
    return true;
}

// Old definitions from `key` that the new file no longer defines.
static void note_removed(VirtualMachine *vm, Obj *old_head, const char *key,
                         const char *path, ReloadStats *st) {
    for (Obj *o = old_head; o; o = o->next) {
        if (!is_file_scope_obj(o) || !is_defined(o) || !obj_in_file(o, key))
            continue;
        bool still = false;
        for (Obj *nw = vm->compiler.globals; nw != old_head && !still;
             nw      = nw->next)
            still =
                is_file_scope_obj(nw) && is_defined(nw) && same_identity(nw, o);
        // Only the newest Obj of a name is reported.
        for (Obj *p = old_head; p != o && !still; p = p->next)
            if (is_file_scope_obj(p) && is_defined(p) && same_identity(p, o))
                still = true;
        if (!still) {
            printf("note: %s removed from %s; the old definition is kept\n",
                   o->name, path);
            st->removed++;
        }
    }
}

// Drops the Objs that only repeat a live one: their references are resolved,
// and later lookups by name should find the live Obj.
static void unlink_reused(VirtualMachine *vm, Obj *old_head) {
    Obj **link = &vm->compiler.globals;
    while (*link != old_head) {
        if ((*link)->reuse_of)
            *link = (*link)->next;
        else
            link = &(*link)->next;
    }
}

// Makes the file's new definitions reachable by name from the live scope.
static void publish_new(VirtualMachine *vm, Obj *old_head) {
    for (Obj *nw = vm->compiler.globals; nw != old_head; nw = nw->next) {
        if (!is_file_scope_obj(nw) || nw->reuse_of || nw->reload_of ||
            !is_defined(nw))
            continue;
        push_scope(vm, nw->name, (int)strlen(nw->name))->var = nw;
    }
}

static bool has_old_definition_in(Obj *old_head, const char *key) {
    for (Obj *o = old_head; o; o = o->next)
        if (is_file_scope_obj(o) && o->is_function && o->body &&
            obj_in_file(o, key))
            return true;
    return false;
}

int cc_debug_reload(VirtualMachine *vm, const char *path) {
    if (vm->dbg.host_fault_signal) {
        fprintf(stderr, "error: cannot reload after a host fault; execution "
                        "cannot resume\n");
        return -1;
    }
    if (vm->compiler.compile_only) {
        fprintf(stderr, "error: reload is VM-only\n");
        return -1;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        fprintf(stderr, "error: reload: cannot open '%s'\n", path);
        return -1;
    }

    Obj *old_head = vm->compiler.globals;
    if (!has_old_definition_in(old_head, resolved)) {
        fprintf(stderr, "error: reload: '%s' is not a file of this program\n",
                path);
        return -1;
    }

    jmp_buf             jb;
    jmp_buf            *saved_jb     = vm->error_jmp_buf;
    Obj                *saved_fn     = vm->compiler.current_fn;
    Obj                *saved_locals = vm->compiler.locals;
    CcPreprocessorState pp           = cc_preprocessor_begin_reload(vm);
    volatile bool       pp_active    = true;
    Scope *volatile fresh            = NULL;
    MacroFn      *old_fns              = vm->compiler.macro_fns;
    ComptimeVar  *old_vars             = vm->compiler.comptime_vars;
    CompileError *err_tail             = vm->errors_tail;
    int           err_count            = vm->error_count;
    CcCodeMark  mark                 = {0};
    ReloadStats st                   = {0};
    int         rc                   = -1;
    vm->error_jmp_buf                = &jb;

    if (setjmp(jb) == 0) {
        Token *tok = cc_preprocess_ex(vm, resolved, false);
        if (!tok)
            error("failed to preprocess %s", path);
        cc_preprocessor_end_reload(vm, pp);
        pp_active        = false;

        if (!settle_comptime(vm, old_fns, old_vars))
            goto refused;
        Token *call = cc_find_file_scope_comptime_call(vm, tok);
        if (call) {
            char *name = strndup(call->loc, (size_t)call->len);
            reload_fail(vm, "file-scope comptime call '%s' cannot be reloaded",
                        name, NULL);
            free(name);
            goto refused;
        }

        fresh = cc_parse_file_isolated(vm, tok);
        if (vm->error_count > err_count)
            goto refused;

        const char *kind = NULL;
        char       *name =
            (char *)changed_declaration(vm->compiler.scope, fresh, &kind);
        if (name) {
            reload_fail(vm, "%s '%s' changes layout; nothing reloaded", kind,
                        name);
            free(name);
            goto refused;
        }
        if (!match_objects(vm, old_head, &st))
            goto refused;
        if (!no_comptime_calls(vm, old_head))
            goto refused;

        vm->compiler.current_fn = NULL;
        vm->compiler.locals     = NULL;
        cc_repl_compile_new_marked(vm, old_head, &mark);
        vm->compiler.current_fn = saved_fn;
        vm->compiler.locals     = saved_locals;

        note_removed(vm, old_head, resolved, path, &st);
        publish_new(vm, old_head);
        unlink_reused(vm, old_head);
        debugger_reresolve_breakpoints(vm);
        printf("reloaded %s: %d changed, %d unchanged, %d new\n", path,
               st.changed, st.unchanged, st.added);
        rc = 0;
        goto done;
    }

    // An error from the preprocessor, parser or code generator.
    if (vm->error_message) {
        fputs(vm->error_message, stderr);
        vm->error_message = NULL;
    }
    if (pp_active)
        cc_preprocessor_end_reload(vm, pp);
    if (mark.started)
        cc_code_discard(vm, &mark);
refused:
    vm->compiler.globals       = old_head;
    vm->compiler.macro_fns     = old_fns;
    vm->compiler.comptime_vars = old_vars;
done:
    flush_errors(vm, err_tail, err_count);
    vm->error_jmp_buf       = saved_jb;
    vm->compiler.current_fn = saved_fn;
    vm->compiler.locals     = saved_locals;
    if (fresh) {
        hashmap_deinit_borrowed(&fresh->var_map);
        hashmap_deinit_borrowed(&fresh->tag_map);
    }
    return rc;
}
