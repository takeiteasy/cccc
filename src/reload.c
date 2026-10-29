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
// reload leaves the session as it was. A changed function's calls to comptime
// functions are expanded against the session's retained comptime program. A
// file that changes or adds a comptime function or variable builds that program
// again first (cc_comptime_reload), and the file's functions that call
// comptime code are expanded again. Its file-scope comptime calls run again
// (cc_run_file_scope_calls) and what they generate is matched like the file's
// own definitions.

#include "internal.h"
#include "parse_internal.h"
#include "watch.h"

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
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

// The file a file-scope Obj was written in, or whose comptime call made it.
static const File *obj_file(const Obj *o) {
    if (obj_fn(o)->gen_site)
        return obj_fn(o)->gen_site->file;
    if (obj_fn(o)->def_begin && obj_fn(o)->def_begin->file)
        return obj_fn(o)->def_begin->file;
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
    return o->name && o->name[0] != '.' && !obj_fn(o)->static_owner && !o->is_local &&
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
    return o->is_function ? obj_fn(o)->body != NULL : o->is_definition;
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
    const Token *ta = obj_fn(a)->def_begin, *tb = obj_fn(b)->def_begin;
    if (!ta || !tb || !obj_fn(a)->def_end || !obj_fn(b)->def_end)
        return false;
    for (;; ta = ta->next, tb = tb->next) {
        if (ta->kind != tb->kind || ta->len != tb->len ||
            memcmp(ta->loc, tb->loc, (size_t)ta->len))
            return false;
        if (ta == obj_fn(a)->def_end || tb == obj_fn(b)->def_end)
            return ta == obj_fn(a)->def_end && tb == obj_fn(b)->def_end;
    }
}

static void reload_fail(VirtualMachine *vm, const char *fmt, const char *a,
                        const char *b) {
    char msg[512];
    snprintf(msg, sizeof(msg), fmt, a, b ? b : "");
    fprintf(stderr, "error: reload: %s\n", msg);
    (void)vm;
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
static bool match_objects(VirtualMachine *vm, Obj *old_head, ReloadStats *st,
                          bool comptime_rebuilt) {
    for (Obj *nw = vm->compiler.globals; nw != old_head; nw = nw->next) {
        if (!is_file_scope_obj(nw))
            continue;
        Obj *old = find_old(vm->compiler.globals, old_head, nw);

        if (nw->is_function) {
            if (!obj_fn(nw)->body)
                continue;
            if (!old || !obj_fn(old)->body) {
                st->added++;
                continue;
            }
            if (!types_same_layout(old->ty, nw->ty) ||
                old->is_inline != nw->is_inline) {
                reload_fail(vm, "'%s' changes its signature", nw->name, NULL);
                return false;
            }
            // A rebuilt comptime program can change what a call expands to, and
            // a generated function has no source to compare.
            bool stale =
                obj_fn(nw)->gen_site ||
                (comptime_rebuilt && obj_fn(nw)->def_begin &&
                 cc_find_comptime_call(vm, obj_fn(nw)->def_begin, obj_fn(nw)->def_end));
            if (bodies_equal(old, nw) && !stale) {
                obj_fn_mut(vm, nw)->reuse_of = old;
                st->unchanged++;
            } else {
                obj_fn_mut(vm, nw)->reload_of = reuse_root(old);
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
            obj_fn_mut(vm, nw)->reuse_of = old;
    }
    return true;
}

// A rebuilt comptime program does not reach the functions of other files.
static void note_stale_callers(VirtualMachine *vm, Obj *old_head,
                               const char *key) {
    for (Obj *o = old_head; o; o = o->next)
        if (is_file_scope_obj(o) && o->is_function && obj_fn(o)->body && obj_fn(o)->def_begin &&
            !obj_in_file(o, key) &&
            cc_find_comptime_call(vm, obj_fn(o)->def_begin, obj_fn(o)->def_end)) {
            printf("note: comptime code changed; reload the other files that "
                   "call it\n");
            return;
        }
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
        if (obj_fn((*link))->reuse_of)
            *link = (*link)->next;
        else
            link = &(*link)->next;
    }
}

// Makes the file's new definitions reachable by name from the live scope.
static void publish_new(VirtualMachine *vm, Obj *old_head) {
    for (Obj *nw = vm->compiler.globals; nw != old_head; nw = nw->next) {
        if (!is_file_scope_obj(nw) || obj_fn(nw)->reuse_of || obj_fn(nw)->reload_of ||
            !is_defined(nw))
            continue;
        push_scope(vm, nw->name, (int)strlen(nw->name))->var = nw;
    }
}

static bool has_old_definition_in(Obj *old_head, const char *key) {
    for (Obj *o = old_head; o; o = o->next)
        if (is_file_scope_obj(o) && o->is_function && obj_fn(o)->body &&
            obj_in_file(o, key))
            return true;
    return false;
}

// quiet_noop: a file with nothing to redefine prints nothing (--watch reloads
// every file of the program on each change).
static int reload_file(VirtualMachine *vm, const char *path, bool quiet_noop) {
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
    if (!has_old_definition_in(old_head, resolved) &&
        !hashmap_get(&vm->compiler.tu_deps, resolved)) {
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
    HashMap            *saved_deps   = vm->compiler.dep_set;
    volatile bool       deps_changed = false;
    Scope *volatile fresh            = NULL;
    MacroFn      *old_fns            = vm->compiler.macro_fns;
    int           old_pending        = vm->compiler.comptime_pending_includes.len;
    CcComptimeReload ctr             = {0};
    ComptimeVar  *old_vars           = vm->compiler.comptime_vars;
    Obj             *old_macro_globals  = vm->compiler.macro_globals;
    CompileError *err_tail           = vm->errors_tail;
    int           err_count          = vm->error_count;
    CcCodeMark    mark               = {0};
    ReloadStats   st                 = {0};
    int           rc                 = -1;
    vm->error_jmp_buf                = &jb;

    if (setjmp(jb) == 0) {
        Token *tok   = cc_preprocess_ex(vm, resolved, false);
        deps_changed = true;
        if (!tok)
            error("failed to preprocess %s", path);
        cc_preprocessor_end_reload(vm, pp);
        pp_active = false;

        if (!cc_comptime_reload(vm, old_fns, old_vars, old_pending, &ctr))
            goto refused;
        Obj *generated = cc_run_file_scope_calls(vm, &tok);
        // The parse resolves the generated globals' extern declarations.
        vm->compiler.macro_globals = generated;
        fresh                      = cc_parse_file_isolated(vm, tok);
        vm->compiler.macro_globals = old_macro_globals;
        if (vm->error_count > err_count)
            goto refused;
        if (generated) {
            Obj *tail = generated;
            while (tail->next)
                tail = tail->next;
            tail->next           = vm->compiler.globals;
            vm->compiler.globals = generated;
        }

        const char *kind = NULL;
        char       *name =
            (char *)changed_declaration(vm->compiler.scope, fresh, &kind);
        if (name) {
            reload_fail(vm, "%s '%s' changes layout; nothing reloaded", kind,
                        name);
            free(name);
            goto refused;
        }
        if (!match_objects(vm, old_head, &st, ctr.rebuilt))
            goto refused;
        cc_session_expand_macros(vm, old_head);

        vm->compiler.current_fn = NULL;
        vm->compiler.locals     = NULL;
        cc_repl_compile_new_marked(vm, old_head, &mark);
        vm->compiler.current_fn = saved_fn;
        vm->compiler.locals     = saved_locals;

        note_removed(vm, old_head, resolved, path, &st);
        if (ctr.rebuilt)
            note_stale_callers(vm, old_head, resolved);
        publish_new(vm, old_head);
        unlink_reused(vm, old_head);
        debugger_reresolve_breakpoints(vm);
        if (!quiet_noop || st.changed || st.added)
            printf("reloaded %s: %d changed, %d unchanged, %d new\n", path,
                   st.changed, st.unchanged, st.added);
        cc_deps_commit(vm);
        cc_comptime_reload_commit(&ctr);
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
    if (!deps_changed)
        cc_deps_abort(vm, saved_deps);
refused:
    cc_comptime_reload_rollback(vm, &ctr);
    if (deps_changed)
        cc_deps_rollback(vm, resolved);
    vm->compiler.globals       = old_head;
    vm->compiler.macro_fns     = old_fns;
    vm->compiler.comptime_vars = old_vars;
    vm->compiler.macro_globals = old_macro_globals;
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

int cc_debug_reload(VirtualMachine *vm, const char *path) {
    return reload_file(vm, path, false);
}

volatile sig_atomic_t _cccc_watch_pending;

void cc_watch_notify(void) {
    _cccc_watch_pending = 1;
    _cccc_any_pending   = 1;
}

typedef struct {
    char **changed;
    bool  *covered;
    int    n;
    char **roots;
    int    count, cap;
} ApplyPlan;

static void plan_add(ApplyPlan *p, const char *path) {
    for (int i = 0; i < p->count; i++)
        if (!strcmp(p->roots[i], path))
            return;
    if (p->count == p->cap) {
        p->cap   = p->cap ? p->cap * 2 : 8;
        p->roots = realloc(p->roots, (size_t)p->cap * sizeof *p->roots);
    }
    p->roots[p->count++] = strdup(path);
}

// A root is reloaded when it, or a file it read, changed.
static int plan_root_iter(char *key, int keylen, void *val, void *user_data) {
    ApplyPlan *p   = user_data;
    char      *root = strndup(key, (size_t)keylen);
    for (int i = 0; i < p->n; i++)
        if (hashmap_get((HashMap *)val, p->changed[i])) {
            p->covered[i] = true;
            plan_add(p, root);
        }
    free(root);
    return 0;
}

static int watch_dep_iter(char *key, int keylen, void *val, void *user_data) {
    (void)val;
    (void)user_data;
    char *path = strndup(key, (size_t)keylen);
    cc_watch_add(path);
    free(path);
    return 0;
}

// Reloads the files that read a changed path. A changed file that no root
// read (one the REPL loaded) is reloaded on its own when it defines functions.
void cc_watch_apply(VirtualMachine *vm) {
    // A reload can run guest code (a new global's initializer), which reaches
    // the safe point again; the nested save waits for the outer reload.
    static bool applying;
    if (applying) {
        _cccc_watch_pending = 1;
        return;
    }
    applying = true;

    ApplyPlan plan = {0};
    plan.changed   = cc_watch_take(&plan.n);
    plan.covered   = calloc((size_t)plan.n + 1, sizeof(bool));
    hashmap_foreach(&vm->compiler.tu_deps, plan_root_iter, &plan);
    for (int i = 0; i < plan.n; i++)
        if (!plan.covered[i] && access(plan.changed[i], R_OK) == 0 &&
            has_old_definition_in(vm->compiler.globals, plan.changed[i]))
            plan_add(&plan, plan.changed[i]);
    for (int i = 0; i < plan.n; i++)
        free(plan.changed[i]);
    free(plan.changed);
    free(plan.covered);

    char **owners = plan.roots;
    int    count  = plan.count;

    char cwd[PATH_MAX], raw_cwd[PATH_MAX];
    if (!getcwd(raw_cwd, sizeof raw_cwd) || !realpath(raw_cwd, cwd))
        cwd[0] = 0;
    size_t cwd_len = strlen(cwd);
    for (int i = 0; i < count; i++) {
        const char *shown = owners[i];
        if (cwd_len && !strncmp(shown, cwd, cwd_len) && shown[cwd_len] == '/')
            shown += cwd_len + 1;
        if (reload_file(vm, shown, true) == 0 && cc_watch_active()) {
            HashMap *deps = hashmap_get(&vm->compiler.tu_deps, owners[i]);
            if (deps)
                hashmap_foreach(deps, watch_dep_iter, NULL);
        }
        free(owners[i]);
    }
    free(owners);
    if (cc_watch_active())
        cc_watch_arm();

    applying = false;
    if (_cccc_watch_pending)
        _cccc_any_pending = 1;
}
