// AST to source code serialization
// Converts AST nodes back to C source text for -M pragma macro expansion output
//
// Shared declarations for the src/serialize_*.c translation units (ticket
// #1150 split of the former monolithic src/serialize.c). Nothing in here is
// public API; it exists only to let the split files call into each other.
#ifndef CCCC_SERIALIZE_INTERNAL_H
#define CCCC_SERIALIZE_INTERNAL_H

#include "./internal.h"

typedef struct {
    Type **data;
    int    len;
    int    cap;
} TypeVec;

typedef struct {
    Type *ty;
    char *name;
    int   name_len;
    Obj  *owner_fn;
    // #891: mirrors TypeNameRecord.from_include/always_emit (cccc.h) -- used
    // in !generated_only mode (-c=native, -m without -c=generated) to avoid
    // re-emitting a definition the consumer's own #include already provides.
    bool from_include;
    bool always_emit;
    // #953: mirrors TypeNameRecord.file_path -- used in generated_only mode
    // to tell whether this type's declaring header was actually captured
    // into the output.
    char *file_path;
    // #1010: mirrors TypeNameRecord.defines_type -- see that field's comment
    // (cccc.h) and find_tag_name_for_provenance() below.
    bool defines_type;
} TypeName;

// #965: pairs a lifted block function (Obj.is_block) with the name of the
// environment struct serialize_block_preamble() emitted for it, so
// serialize_expr's ND_BLOCK_LITERAL/ND_BLOCK_CALL cases and ND_VAR's
// captured-variable lookup can find it again without re-deriving it. Built
// once in serialize_block_preamble(), read-only afterward.
typedef struct {
    Obj  *block_fn;
    char *env_struct_name; // includes the leading "struct " keyword
} BlockEnvEntry;

// #1074: the native-serializer mirror of a GNU nested function's static
// link. One entry per function that directly parents at least one
// Obj.is_nested function -- even a function whose own locals are never
// referenced by a descendant still needs an entry (with an empty upvars
// list) purely to carry `__up` for an intervening level of a multi-level
// nest. `upvars` holds `owner_fn`'s own locals/params that some nested
// descendant (at any depth) reads or writes, in first-seen order -- that
// order is also each var's field index (`__uv<index>`). Built once in
// serialize_nested_preamble(), read-only afterward; mirrors BlockEnvEntry/
// serialize_block_preamble()'s shape exactly, just keyed by the parent
// function instead of the block literal.
typedef struct {
    Obj  *owner_fn;
    char *env_struct_name; // includes the leading "struct " keyword
    Obj **upvars;
    int   upvars_len;
    int   upvars_cap;
} NestedEnvEntry;

// #1044: one function-local `static` (or block-scope compound literal)
// whose initializer takes a label's address (`&&label`, GNU labels-as-
// values) and so cannot be hoisted to file scope the way rename_anon_
// globals() ordinarily hoists an anonymous global -- labels-as-values are
// only legal inside the function that defines the label (verified directly
// against real clang/GCC). Emitted instead inside owner_fn's own body by
// serialize_function(), right after the flat local-declaration hoist. See
// collect_deferred_static_labels()'s own comment.
typedef struct {
    Obj *var;
    Obj *owner_fn;
} DeferredStaticLabel;

// #1044: one `ND_LABEL` reachable from some function's body, recording both
// spellings a Relocation might need to resolve against: `unique_label` (a
// `.L..N` string, matched by pointer identity -- resolve_goto_labels(),
// parse_decl.c, hands the identical pointer to a label and to every
// reference that resolves against it, exactly like SerJumpFrame's
// brk_label/cont_label matching above) and `label` (the real source
// spelling a `&&L0` reference must print).
typedef struct {
    char *unique_label;
    char *label;
    Obj  *owner_fn;
} LabelOwner;

// #1005: break/continue lower to an ND_GOTO with only `unique_label` set (a
// non-C-identifier ".L..N" string, parse.c's new_unique_name()) -- a source
// `goto` sets `label` instead. Serializing such an ND_GOTO back to a literal
// `break;`/`continue;` needs to know, at the point it's printed, which
// enclosing construct `unique_label` refers to. This mirrors NNJumpTarget/
// nn_find_target (parse.c's null-neighbor analysis) exactly, including its
// pointer-identity matching rule: new_unique_name() hands the identical
// string pointer to both a construct's brk_label/cont_label and to every
// break/continue targeting it, so `==` (not strcmp) is correct and cheap.
typedef struct SerJumpFrame {
    struct SerJumpFrame *parent;
    char *brk_label;  // NULL if this construct isn't a break target
    char *cont_label; // NULL for switch -- parse.c's switch parsing only
                      // saves/restores brk_label, never cont_label, so a
                      // `continue` inside a switch inside a loop must skip
                      // over the switch frame and resolve to the loop.
} SerJumpFrame;

// #1015: one renamed enumerator, keyed by the enum Type it belongs to (via
// same_type_strong()) plus its original spelling -- see the ctx->
// enum_renames doc comment on SerializeContext for why this is a table
// rather than an in-place EnumConstant.name mutation.
typedef struct {
    Type *rep;
    char *orig;
    char *new_name;
} EnumConstRename;

typedef struct {
    TypeVec   seen;
    TypeVec   defs;
    TypeName *tags;
    int       tags_len;
    int       tags_cap;
    TypeName *typedefs;
    int       typedefs_len;
    int       typedefs_cap;
    Obj      *current_fn;
    bool generated_only; // skip header typedefs; output is consumed alongside
                         // normal headers
    // #891: --emit-only suppresses auto-capture (preprocess.c), so under it
    // the primary file's own #include directives are NOT re-emitted -- a
    // header-sourced typedef/tag has no re-emitted #include to collide with
    // and must still be serialized. Only skip has_include gates when this
    // is false.
    bool emit_strict;
    bool emit_cccc; // --emit-cccc: serialize checked-pointer qualifiers instead
                    // of dropping them
    // #1172: mirrors !vm->compiler.no_layout_guards -- true unless
    // --no-layout-guards was given. Read by serialize_layout_guards() (which
    // also independently bails under emit_cccc/emit_strict; see its own
    // comment) so a single flag check at each aggregate-def call site covers
    // every guard-emission path.
    bool emit_layout_guards;
    int  anon_local_counter; // names compiler-synthesized temps (e.g. ++/--
                             // desugaring)
    int anon_global_counter; // names non-string-literal `.L..N` globals (#925)
    // #953: resolved paths of headers actually auto-captured into
    // generated_only (-c=generated) output -- built once in
    // cc_serialize_program from vm->compiler.emit_include_paths. Only
    // consulted in generated_only mode; see serialize_type_defs_for_owner.
    // A VLA's length is an expression node, so serializing its declarator
    // (serialize_type_decl, which has no vm parameter) needs the vm the
    // expression serializer takes. Set once in cc_serialize_program.
    VirtualMachine *vm;
    char          **captured_paths;
    int             captured_paths_len;
    // #1292: memoizes cc_canonical_path_key()'s realpath() call for
    // path_is_captured()'s query side (raw path -> canonical key) --
    // path_is_captured() runs once per bodiless Obj/TypeName lookup across
    // the whole program, and realpath() is a syscall the #1283 perf wall
    // work made a point of keeping off any whole-program-scale path.
    // Zero-initialized like every other ad-hoc HashMap field on this
    // struct (see e.g. the `anchors`/`claimed` locals in
    // rename_colliding_static_names).
    HashMap path_key_memo;
    // #1302: name -> Obj* for every entry of vm->compiler.globals, and
    // name -> 1 for every file-scope (owner_fn == NULL) ctx->typedefs
    // entry -- both lazily built on first use (built_global_names /
    // built_typedef_names guard a one-time populate, see
    // ensure_global_name_index()/ensure_file_typedef_name_index() in
    // serialize_decl.c) so a program that hits neither collision class
    // pays nothing beyond the guard check. Consulted by serialize_
    // function()'s hoisted-local collision loop (the #926 do-while) to
    // catch a hoisted local's name colliding with a global or file-scope
    // typedef that the same function still references outside the
    // local's original (narrower, pre-hoist) scope -- see that loop's own
    // #1302 comment.
    HashMap global_names;
    HashMap file_typedef_names;
    bool    built_global_names;
    bool    built_file_typedef_names;
    // #965: block-literal env structs -- see BlockEnvEntry and
    // serialize_block_preamble().
    BlockEnvEntry *block_envs;
    int            block_envs_len;
    int            block_envs_cap;
    // #1074: nested-function env structs -- see NestedEnvEntry and
    // serialize_nested_preamble(). Built (and every illegal non-call
    // reference to a nested function rejected) entirely during that one
    // preamble pass, before any function body is actually emitted -- so
    // serialize_expr's own ND_VAR/ND_FUNCALL arms never need to re-derive
    // "is this reference legal", only "what does a known-legal one print
    // as".
    NestedEnvEntry *nested_envs;
    int             nested_envs_len;
    int             nested_envs_cap;
    // #1044: every ND_LABEL reachable from any function's body (built by
    // collect_deferred_static_labels()), and the anonymous globals whose
    // relocations resolve against that table -- see DeferredStaticLabel/
    // LabelOwner's own comments.
    LabelOwner          *label_owners;
    int                  label_owners_len;
    int                  label_owners_cap;
    DeferredStaticLabel *deferred_label_statics;
    int                  deferred_label_statics_len;
    int                  deferred_label_statics_cap;
    // #989: types promoted from function-local to file scope (a block
    // capture's own struct/union/enum type declared inside a function,
    // needed because its lifted environment struct is emitted at file
    // scope). Doubles as the post-order seen-set during promotion and as
    // the skip-set serialize_type_defs_for_owner uses to avoid re-emitting
    // a definition the preamble already wrote out.
    TypeVec hoisted;
    int     hoisted_type_counter; // names renamed/synthesized hoisted tags,
                                  // parallel to anon_global_counter
    // #1046: struct/union/enum bodies actually printed so far, across BOTH
    // emitters that can print one -- the usage-driven ctx->defs loop in
    // serialize_type_defs_for_owner() and the typedef-dependency-driven
    // emit_typedef_and_deps() below (an anonymous aggregate whose only
    // spelling is a typedef alias, e.g. `typedef struct { int a[2]; } P,
    // *Pp;`, is never pushed into ctx->defs at all when P itself is never
    // used by value -- see emit_typedef_and_deps's own comment). Shared
    // dedup set so neither emitter re-prints what the other already did.
    TypeVec emitted_defs;
    // #1005: current break/continue target stack (innermost first) and the
    // innermost enclosing ND_SWITCH (for ND_CASE to test default_case
    // against) -- both NULL outside any loop/switch, pushed/popped around
    // ND_FOR/ND_DO/ND_SWITCH bodies in serialize_stmt.
    SerJumpFrame *jumps;
    Node         *cur_switch;
    // #1014: set by rename_colliding_type_tags() iff it actually renamed a
    // colliding struct/union/enum tag -- gates find_tag_name()'s extra
    // completeness-preferring scan so a program with no collision is
    // unaffected (byte-identical output).
    bool tag_renamed;
    // #1015: colliding-enumerator rename table, built by
    // rename_colliding_enum_constants() (right after rename_colliding_
    // type_tags()). Deliberately a print-time lookup table, not a mutation
    // of EnumConstant.name -- same_type_or_origin()'s TY_ENUM arm compares
    // enumerators by strcmp, and every consumer of it (collect_type,
    // type_vec_contains/find, find_tag_name) runs after this pass; mutating
    // one Type's enumerator and not a structurally-identical sibling's
    // would break that equality and re-introduce the collision as a
    // full-definition dup instead. Consulted only by serialize_enum_def via
    // enum_const_spelling().
    EnumConstRename *enum_renames;
    int              enum_renames_len;
    int              enum_renames_cap;
    // #1085: bumped once per va_list-forwarding call-site shim emitted (see
    // ND_FUNCALL below) so nested/repeated occurrences in one TU each get
    // their own __cccc_va_fwd_N local instead of shadowing.
    int va_fwd_seq;
    // #1095: true only while serialize_type_decl() is emitting the
    // declarator for a local, or a global with no byte-image initializer
    // (var->init_data == NULL) -- the two shapes where re-materializing a
    // host-owned sizeof/_Alignof array dimension (Type.array_len_layout_ty)
    // can't disagree with anything else CCCC also emits for the same
    // object. False everywhere else on purpose: a struct/union MEMBER's
    // array dimension must stay folded (the containing aggregate's layout,
    // and every other member's offset, is computed from that folded value
    // -- re-materializing would desync them, the same reasoning that rules
    // out bitfield widths, see man/NATIVE.md), an INITIALIZED global's
    // dimension must stay folded (serialize_init_bytes' own byte image is
    // still sized off the folded value -- re-materializing only the
    // dimension would make the array declaration and its initializer
    // disagree), and a typedef/cast/type-name spelling must stay folded
    // (reused across every context, including the two excluded above).
    bool allow_layout_dims;
    // #1113(a): set by collect_type() the moment any TY_DECIMAL32/64/128
    // is reached during the pre-emission collection walk (cc_serialize_
    // program's collect_obj_types()/collect_static_assert_types() loop) --
    // a global/local declaration, a struct member, or a decimal literal's
    // own node->ty all funnel through collect_type() before any real
    // output is written, so this is known before the preamble is printed.
    // Read once by serialize_decimal_native_guard() (serialize_program.c)
    // to decide whether to emit the guarded #error preamble; see that
    // function's own comment for why gcc must not be affected.
    bool saw_decimal;
    // #1123: bumped once per wide-_BitInt (N>128) statement-expression
    // lowering emitted (serialize_expr.c) so nested/repeated occurrences in
    // one TU each get their own __cccc_bi_<n> temporaries instead of
    // shadowing -- same reasoning as va_fwd_seq above, and for the same
    // reason (these constructs nest: e.g. a masked cast of a binop result).
    int wide_bitint_seq;
} SerializeContext;

typedef struct {
    Obj **data;
    int   len;
    int   cap;
} ObjVec;

// ========== Cross-file forward declarations ==========
//
// Every function below is used from at least one serialize_*.c file
// other than the one that defines it, so each lost its `static` and
// gained a prototype here (#1150 split of the former monolithic
// src/serialize.c).

bool atomic_serializable_pointee(Node *addr);
bool function_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *obj);
bool global_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                               Obj *obj);
bool is_noop_expr(Node *node);
bool nested_upvar_is_deferred(Obj *var);
bool nested_var_is_own(Obj *fn, Obj *var);
bool obj_vec_contains(ObjVec *vec, Obj *obj);
bool path_basename_is(const char *path, const char *name);
bool path_is_captured(SerializeContext *ctx, const char *path);
bool same_type_strong(Type *a, Type *b);
bool serialize_flonum_special(FILE *f, long double v, const char *suf);
bool serialize_layout_const(FILE *f, SerializeContext *ctx, Type *layout_ty,
                            bool is_align);
bool type_has_tag_for_owner(SerializeContext *ctx, Type *ty, Obj *owner_fn);
bool type_is_cccc_va_list(Type *ty);
bool type_is_complete_tagged(Type *ty);
bool type_layout_is_host_owned(SerializeContext *ctx, Type *ty, int depth);
bool type_needs_anon_aggregate(SerializeContext *ctx, Type *ty);
bool type_vec_contains(TypeVec *vec, Type *ty);
bool typedef_alias_header_suppressed(SerializeContext *ctx, TypeName *alias);
bool var_is_deferred_label_static(SerializeContext *ctx, Obj *var);
const char *enum_const_spelling(SerializeContext *ctx, Type *ty,
                                const char *name);
const char *find_block_env(SerializeContext *ctx, Obj *block_fn);
const LabelOwner *find_label_owner(SerializeContext *ctx, const char *name);
int block_capture_index(Obj *block_fn, Obj *var);
Obj *serialize_find_global(VirtualMachine *vm, const char *name);
Type *nested_upvar_field_type(VirtualMachine *vm, Obj *var);
TypeName *find_typedef_name(SerializeContext *ctx, Type *ty);
TypeName *find_typedef_name_exact(SerializeContext *ctx, Type *ty);
TypeName *find_generated_uncaptured_typedef(SerializeContext *ctx,
                                            Type             *ty); // #1241
unsigned __int128 decode_wide_digits(const char *digits, int base);
void collect_generated_call_targets(Node *node, ObjVec *out);
void collect_obj_types(SerializeContext *ctx, Obj *obj);
void collect_static_assert_types(SerializeContext *ctx, Node *cond); // #1167
void collect_scope_names(SerializeContext *ctx, VirtualMachine *vm);
void format_float_literal(char *buf, size_t cap, double v);
void format_ldouble_literal(char *buf, size_t cap, long double v);
void hoist_local_type_to_file_scope(FILE *f, VirtualMachine *vm,
                                    SerializeContext *ctx, Type *ty);
void obj_vec_push(ObjVec *vec, Obj *obj);
void print_indent_level(FILE *f, int indent);
void reorder_defs_by_byval_deps(SerializeContext *ctx);
void serialize_alignas_if_needed(FILE *f, Obj *var);
void serialize_atomic_addr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                           Node *addr);
void serialize_canonical_const_shims(FILE *f, VirtualMachine *vm, Obj *prog);
void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                    Node *node, int parent_prec);
// #1235: like serialize_expr(), but for a value-discarding position (an
// expression statement, a for-loop update clause). If `node` is the ND_CAST
// new_inc_dec() tags with is_inc_dec_result, emits only the underlying
// `(tmp = &A, *tmp = *tmp + 1)` store -- dropping the cast and the dead
// `+ -addend` term that otherwise trips -Wunused-value. Any other node is
// forwarded to serialize_expr() unchanged.
void serialize_discard_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                            Node *node, int parent_prec);
void serialize_function(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                        Obj *fn);
// #1044/#1302: iterative (explicit stack), pointer-identity-deduped AST
// walk -- safe over cccc's own heavily-shared Node graph, unlike naive
// recursion. Shared between serialize_program.c's label/var-ref passes and
// serialize_decl.c's hoisted-local-vs-referenced-global collision check.
void serialize_ast_walk(Node *root, void (*visit)(Node *, void *), void *ctx);
// #1253: give a fresh source spelling to each duplicate ND_LABEL a hygienic
// Quote() template produced in `body`, rewriting references keyed by
// unique_label. Called once per function body before it is emitted.
void serialize_dedupe_function_labels(VirtualMachine *vm, Node *body);
void serialize_function_signature(FILE *f, SerializeContext *ctx, Obj *fn,
                                  bool with_asm_label);
void serialize_global_var(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                          Obj *var);
void serialize_dlfcn_shims(FILE *f, VirtualMachine *vm, Obj *prog);
void serialize_native_accessor_shims(FILE *f, Obj *prog);
void serialize_reallocarray_shim(FILE *f, Obj *prog);
void serialize_posix_compat_shims(FILE *f, VirtualMachine *vm, Obj *prog);
void serialize_c23_fromfp_shims(FILE *f, VirtualMachine *vm,
                                Obj *prog); // #1195
// #1123: emits the __cccc_biK container typedef(s) and the wide-_BitInt
// (N>128) runtime helper functions whenever any are reachable from `prog` --
// see the function's own comment (src/serialize_shims.c) for the collection
// pass. Unlike its neighbours above, deliberately NOT !generated_only-gated:
// this is a language lowering (needed for -c=generated too), not a libc
// replacement tied to the replayed #include pass.
void serialize_wide_bitint_preamble(FILE *f, Obj *prog);
// Returns true iff `ty` is (or contains, through pointer/array/struct/union/
// function nesting) a TY_BITINT wider than 128 bits -- i.e. one with no
// direct host container, requiring the __cccc_biK lowering above. Shared by
// the preamble's own collection pass and by callers elsewhere that need to
// know whether a type touches the lowering at all (serialize_type.c's
// layout_type_needs_collecting, serialize_decl.c's bitfield-member loop).
bool type_has_wide_bitint(Type *ty);
// #1123: wraps a wide-_BitInt(N>128) `node` with __cccc_bitint_nonzero()
// wherever C expects a scalar truth value -- a raw __cccc_biK struct value
// can't appear there directly. Prints nothing and returns false when `node`
// isn't wide; caller falls back to its own plain serialize_expr() call.
// Defined in serialize_expr.c; also used by serialize_stmt.c's if/while/
// do-while/for condition sites.
bool serialize_wide_bitint_truth(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Node *node);
void serialize_static_assert(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                             Node *cond, const char *msg, int msg_len,
                             Token *tok, int indent);
void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                    Node *node, int indent);
void serialize_stmt_list_item(FILE *f, VirtualMachine *vm,
                              SerializeContext *ctx, Node *node, int indent);
void serialize_string_n(FILE *f, const char *str, int len);
void serialize_threads_shims(FILE *f, VirtualMachine *vm, Obj *prog);
void serialize_type(FILE *f, SerializeContext *ctx, Type *ty);
// #1283: dumps same_type_or_origin() call-cost counters to stderr when
// CCCC_TYPE_STATS is set; no-op otherwise. Called once at the end of
// cc_serialize_program().
void serialize_type_stats_report(SerializeContext *ctx);
// #1283: free the type-name candidate index and re-read its env toggles.
// Called after the rename passes and at the end of cc_serialize_program().
void serialize_type_index_reset(void);
// #1283: the same_type_or_origin() (a, b)-result memo. Live for the whole
// serialize pass: begin() before collect_scope_names(), clear() after the
// rename passes (they mutate struct_tag), end() at each cleanup path.
// CCCC_TYPE_SAME_MEMO_DISABLE keeps it off entirely.
void same_type_memo_begin(void);
void same_type_memo_clear(void);
void same_type_memo_end(void);
void serialize_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
                         const char *name);
void serialize_local_var_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
                                   const char *name);
bool serialize_aliased_ptr_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
                                     const char *name);
void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
                                   Obj *owner_fn);
void serialize_uchar_shims(FILE *f, VirtualMachine *vm, Obj *prog);
void type_vec_push(TypeVec *vec, Type *ty);

#endif // CCCC_SERIALIZE_INTERNAL_H
