# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [Unreleased]

- Fixed: with several input files, what a file-scope generator call declares stays in its own file. Two inputs that instantiate a template from their calls link into one instance (they stopped with `redefinition of 'twice__int'`), and two different `Box` templates that each instantiate `Box__int` keep their own types (`incompatible redeclaration of struct 'Box__int'`).
- Changed: a type, typedef or function prototype a file-scope generator call makes is visible in its own input file only. Another input declares a generated function itself (`int made(void);`) and no longer sees a generated typedef.
- Fixed: a checked-pointer parameter bound that names a file global (`count(g)`) resolves in a file that has a file-scope generator call. It reported `undefined variable 'g'`.
- Fixed: a declared name no longer sticks to the type it was declared with. A local after `int a;` or an `auto` variable is not checked for `-Wshadow` under the last declarator's name (it warned `declaration of 'y' shadows an outer variable` with nothing outer named `y`), and an `asm("sym")` label on one declaration no longer applies to later declarations that share its typedef (`extern T b;` then `int b = 4;` failed with `conflicting asm label`). `MakeTypedef()` of a scalar returns an alias type of its own, so `TypeName()` of it is the alias name; it named the shared builtin type.
- Fixed: `#line N` numbers the next line N (it was N+1), so diagnostics and `__LINE__` agree with gcc and clang, and diagnostics print the file name a `#line N "name"` gives. An error inside a template written in a `#pragma cccc comptime` block names the source file (it named `<comptime-include>`).
- Fixed: `TypeName()` returns a tag or typedef name (`Point`, `mylong`), the spelling of a plain builtin (`int`, `unsigned long`) and `NULL` for any other type. It returned the name of a variable or parameter declared with the type (`value`, `i`), or a typedef that aliased the builtin (`intmax_t` for `long`).
- Added: templates in comptime code. `@template`, `@specialize` and `@inst` work in a comptime function, a `#pragma cccc comptime` block and an `@shared` header. The comptime program has its own templates, apart from the runtime program's. They were rejected with `@template is not supported in comptime code`, and an `@inst` of a runtime template in a comptime function worked or failed (`undefined function in macro bytecode`) depending on which program instantiated it first; it is now `'twice' is not a template in comptime code`.
- Added: `InstantiateType(name, args)` and `InstantiateFunction(name, args)` instantiate a runtime template from a comptime handler (`InstantiateType("Arr", "char, 5")`) and return the instance's `Type *` or `Obj *`.
- Added: `InstantiateTypeWith(name, args, n)` and `InstantiateFunctionWith(name, args, n)` take an array of `TemplateArgument` (`TypeArg(type)`, `ValueArg(node)`) instead of text, so a computed `Type *` needs no spelling.
- Added: a file-scope call (`gen();`) sees its own file's templates, so `InstantiateType()`, `InstantiateFunction()` and `Quote("@inst(...)")` work in it, and the file's own code reuses the instances it made. They failed with `'Box' is not a template`. A call that instantiates a template over a type the file declares, where the file's own code then names the same instance, reports `a file-scope call made 'Box__typedef_Point' over its own copy of a type this file declares`.
- Fixed: in a file with a file-scope generator call, a file-scope struct, union or enum used as a template argument (`Box @(Point)`) reported `a struct declared outside a function body cannot be a template argument`.
- Fixed: `@template`, `@inst`, `Name@(...)` and `@name` attributes work inside an emit block (`#pragma cccc emit begin`), including `@test`, `@build` and `@build_target`. They reached the parser as a raw `@` (`expected an expression, found '@'`). `-c=generated` replays the lines as written.
- Added: `t.v...` expands in a declaration at file scope: an array length, an initializer list or a `_Static_assert` (`char flat[(sizeof(g.v) + ... + 0)];`). It stopped with `member pack 'v' must be expanded with '...'`.
- Added: `t.v...` expands in a struct or union body (a member's array length or initializer), and a struct or union template pattern may hold a template pack and an instance member pack together: `char b[(sizeof(Ts) + sizeof(g.v) + ...)]`. It stopped with `member pack 'v' must be expanded with '...'`.
- Fixed: `cccc -E` prints template definitions and instances in their source spelling (`@template(T)`, `@specialize(A)`, `max@(int)(40, 42)`), each on one line, so its output compiles again. It printed the internal `__cccc_template(T)` and split an `@inst` over several lines.
- Changed: a `CompileError` carries the bare message in `text` (no location, source line or caret) beside the rendered `message`.
- Fixed: a `Quote()` template can use `@inst`, so a comptime macro can call a function template or declare a template struct (`Quote("@inst(int) max($1, $2)", a, b)`). It was rejected with `expected an expression, found '@'`, and an instance made while expanding a function body was left out of the program (`undefined function: max__int`).
- Added: a function template's pattern may hold a template pack and an instance's member pack together: `f(xs + t.v ...)`, `{xs * t.v ...}` and `(xs * t.v + ...)` pair each element of `xs` with the matching member of `t`. Packs of different lengths are an error.
- Fixed: under `--testing`, a negative test whose call instantiates a template, or defines a nested function, with a recoverable error in its body (such as `no such member`) now passes instead of crashing code generation. A later use of an instance that failed reports `instantiation of 'f__int' failed earlier: <the first error>`.
- Added: `t.v...` expands the members of a template instance's pack (`@template(Ts...) struct Tuple { Ts... v; }` gives `v0`, `v1`, ...) in a call, an initializer list, a fold, `sizeof...(t.v)` and `t.v...[k]`, in any function body. `t` is any expression of the instance type and is repeated for each member. An instance whose selected specialization has no such pack, or a pattern naming packs of different lengths, is an error; a bare `t.v` reports `member pack 'v' must be expanded with '...'`.
- Added: a template pack expansion or fold may sit inside another expansion's pattern: `g(h(xs, xs...)...)` runs the inner expansion for each outer element, and `f((xs + ... + 0) * xs ...)` nests a fold. The outer pattern must name a pack outside its inner expansions; `g(h(xs...)...)` is an error.
- Added: defaulted template parameters before a parameter pack: `@template(T, U = T, Ts...)`. Arguments fill the parameters left to right, so `@inst(int)` is `U = int` with an empty pack, and filled defaults are in the instance name (`@inst(int)` and `@inst(int, int)` are one instance). In a partial specialization the pack variable may stand where the primary has defaulted parameters and then takes their arguments too.
- Added: template parameter packs. `@template(Ts...)` and `@template(T, int... Ns)` take any number of trailing arguments (no 16-argument limit), and the body expands the pack with `sizeof...(Ts)`, `Ts...[k]`, `Pattern...` (once per element, comma separated), `Ts... name` (a member or parameter per element: `name0`, `name1`, ...) and C++17 folds (`(xs + ...)`, `(init op ... op xs)`). An empty pack drops its list item and adjacent comma; `@specialize()` and `@specialize(int, char)` specialise an exact argument list.
- Added: partial template specialization: `@template(T) @specialize(T *) struct Vec {...}` covers every argument list that fits a pattern (pointers, qualifiers, constant-length arrays, function types, exact or bound constants, a repeated variable). The most specialised matching partial wins and an exact `@specialize` beats a partial; ambiguous partials are an error.
- Added: a partial specialization pattern may name an instance of a struct, union or enum template (`@specialize(Pair@(T, U))`, `const Pair@(T, U) *`) and bind an array length (`@specialize(T[N])`, `@specialize(T[N], N)`). A constant variable may appear more than once and must then agree. A pattern naming a function or typedef template instance, or `T[N + 1]`, is an error.
- Added: partial specialization of a template with a parameter pack. A pattern names its leading arguments and ends in a pack variable that takes the rest: `@template(T, Ts...) @specialize(T *, Ts...)`, `@specialize(int, Ts...)`, `@specialize(T, 0, Ms...)` for a constant pack. The partial's body expands the variable like any pack (`sizeof...(Ts)`, `Ts... name`, folds), and a pattern may name an instance of a variadic template (`@specialize(Pack@(T, Ts...))`).
- Added: constant template parameter packs of any constant type: `@template(double... Ds)`, `@template(int *... Ps)` and `@template(int (*...Fs)(int))` take floats, object pointers and function pointers, expanded and named like a single float or pointer parameter.
- Added: a partial specialization's constant variables, and its pack variable, may be floats and pointers: `@template(double D) @specialize(D, D)`, `@template(int *P) @specialize(P, P)` and `@template(double... Ds) @specialize(1.0, Ds...)`. A variable binds a parameter of the same kind (integer, floating or pointer) when it holds the value exactly, so a `float F` takes `0.5` but not `0.1`; the body reads the variable like the primary's parameter. A variable of another kind than its parameter is an error, and an instance whose constants are of another kind than a partial's pack variable falls through to the next match.
- Added: a partial specialization may use its pack variable more than once: `@specialize(Pack@(Ts...), Ts...)` matches only when both uses take the same arguments (the same length, and each element the same type or value), for type, integer, float and pointer packs and with element patterns (`Pack@(Ts *...), Ts...`). Two partials whose uses differ in shape, like `Pack@(Ts *...), Ts...` and `Pack@(Ts...), Ts...`, are ambiguous for an instance both match, such as the empty pack.
- Added: a partial specialization pattern may end in an element pattern over a type pack: `@specialize(Ts *...)` matches when every argument is a pointer and `Ts` takes the pointees, `Cell@(Ts)...` and `T, Link@(T, Ts)...` work the same way. `Ts *...` is more specialised than `Ts...`, so it wins when both match; a variable other than the pack must also be deduced by a leading item.
- Changed: a function-local type used as a template argument is named after its function: `struct_S__in_f`, then `struct_S__in_f__2` for a second `struct S` there, in declaration order, so the name no longer depends on the order of the `@inst` uses.
- Fixed: same-named structs, unions and enums from different scopes can all be template arguments: two `struct S` declared in different functions are `struct_S__in_f` and `struct_S__in_g` in instance names (a second one in the same function adds `__2`, in declaration order), and a tagged `struct P` and an untagged `typedef struct {...} P` are different instances (`struct_P`, `typedef_P`). A function instance whose arguments include a type declared inside a function is private to its file instead of being merged with another file's.
- Added: a template argument may be a checked pointer (`Vec__int_ptr_single`, `_array`, `_ntarray`; bounds are dropped), a checked array (`int_arr4_checked`, `_ntchecked`), an address-space type (`global_int_ptr`), a block (`fn_int_of_int_end_block`) or an unnamed struct, union or enum known by its typedef (`typedef_P`). Two arguments that still mangle alike but name different types are an error. `@inst(int) f v;` for a function template reports `template 'f' is a function, not a type`.
- Changed: the first built-in derive attribute (`@equals`, `@ctor`, `@to_string`, ...) in a source file loads `<cccc/derive.h>`, and `@to_json`, `@from_json` and `@json` also load `<cccc/json.h>`, and `@from_string` and `@text` load `<cccc/text.h>`, so the includes are optional. `--no-implicit-derive` restores the explicit-include requirement.
- Changed: a function template instance is shared across translation units: one copy per program, and a pointer to it compares equal everywhere. `static` on the template keeps a private copy per file. Files that define different templates producing the same instance are an error. This also fixes multi-file `-c=native` builds that redefined an instance. Objects from `-c=object` carry the instance as a weak symbol.
- Fixed: `-E` keeps `int * @single q;` on one line instead of breaking after `*`.
- Fixed: `--attr-target=msvc` prints `aligned(N)` as `__declspec(align(N))` and separates `__declspec` items with spaces. Attributes MSVC lacks are dropped, and `packed` warns.
- Changed: the structural markers `comptime`, `test`, `test_setup`, `test_teardown`, `build`, `build_target`, `template` and `specialize` take `cccc::` or no namespace; `[[gnu::test]]`, `[[clang::comptime]]` and `[[gnu::template(T)]]` are errors that name the `cccc::` spelling instead of silently becoming a test or a comptime block.
- Fixed: a file whose only cccc attribute is `test_setup` or `test_teardown` runs in test mode.
- Changed: `__has_attribute` reports `sentinel`, `alloc_size`, `malloc`, `vector_size` and `designated_init`, and `-E` prints them in GNU spelling. A built-in attribute name is never taken over by a `@comptime(attribute("name"))` handler.
- Changed: `-E` prints each attribute in its home namespace, whatever it was written in: `[[cccc::packed]]` prints `__attribute__((packed))` (also under `--emit-cccc`, which keeps only cccc's own attributes as `[[cccc::...]]`), `[[gnu::nodiscard]]` prints `[[nodiscard]]`, `[[gnu::single]]` and `[[single]]` are dropped, and an unknown name in a namespace stays `[[ns::name]]`. A list with several spellings prints one group per spelling. `--attr-target` applies to `-E` only and warns on the command line otherwise.
- Fixed: `packed` on an enum (`enum __attribute__((packed)) E`, `[[gnu::packed]]`, or after the closing brace) gives it the smallest integer type that holds every enumerator, as gcc and clang do, including in template enums and `-c=native` output; an enum with a `: type` base keeps that base.
- Fixed: `-E` prints `[[nodiscard]]` and `[[maybe_unused]]` as `__attribute__((warn_unused_result))` and `__attribute__((unused))` where it spells attributes in GNU form (below C23, or `--attr-target=gnu`), and drops `[[no_unique_address]]`, so gcc and clang no longer warn about unknown attributes.
- Fixed: `-E` drops the names of `@comptime(attribute("name"))` handlers like other cccc attributes (`[[cccc::name]]` under `--emit-cccc`) instead of printing `__attribute__((name))`.
- Added: an attribute resolves by its name in any spelling and any namespace, so `[[cccc::aligned(16)]]`, `[[gnu::single]]` and `[[clang::aligned(16)]]` work, `__x__` is accepted inside `[[...]]`, and `[[kernel]]`, `[[trusted]]`, `[[operator]]` and `[[method]]` need no `cccc::`; an unknown name in a namespace is a `-Wattributes` warning. `[[...]]` and `__attribute__((...))` share one handler, so the GNU-only attributes (`format`, `error`, `warn_unused_result`, ...) work in `[[gnu::...]]` and the standard ones in `__attribute__`. Every attribute is defined once in `src/attr_registry.c`.
- Added: the template markers have spellings without `@`: `[[cccc::template(T)]]` (or no namespace, or `__attribute__((template(T)))`), `[[cccc::specialize(T)]]`, `__template(T)`, `__specialize(T)` and `__inst(T) Name`; the marker must be the only attribute in its list. Postfix `Name@(T)` stays `@`-only.
- Changed: `__has_c_attribute` reports a known attribute in any namespace (`cccc::aligned`, `clang::aligned`, `gnu::single`), and `__has_c_attribute(aligned)` is true.
- Fixed: an attribute argument spelled like a cccc marker, as in `[[gnu::cleanup(comptime)]] int x;`, no longer turns the declaration into a comptime block and drops it.
- Added: `@template(T)` marks a struct, union or function definition as a template, and `@inst(Type, ...) Name` instantiates it as `Name__Type` in any type or expression position, at file scope or inside a function; function instances are `static`, and attributes such as `@equals` apply to each instance.
- Added: a file-scope `@inst(Type, ...) Name;` creates a template instance without using it, so its derive attributes' functions exist.
- Added: `@template(T, int N)` takes integer constant parameters, bound as constants inside the body and given as constant expressions in `@inst(int, 16) Arr`; the instance is `Arr__int__16`, a negative value is spelled with an `n` prefix, and a value that does not fit the parameter's type is an error.
- Added: `@template` on an enum or a typedef. An enum instance is `Level__char` with enumerators renamed `Level__char__HIGH`; a typedef instance is a file-scope typedef named `Ptr__int`; its declarator may follow a type name in parentheses, as in `@template(T) typedef Count (Fn)(T);`.
- Changed: an enum constant argument to a template names the instance by its enumerator (`Pick__GREEN`, the first with that value; a value with none keeps its number) and a bool argument by `true` or `false`.
- Added: `@template` parameters take defaults, `@template(T, int N = 8)` and `@template(K, V = K)`, so `@inst(int) Arr` may leave out trailing arguments; a default may name earlier parameters, and the instance is named with every argument (`Arr__int__8`) whether or not it was written.
- Added: a template instance is also spelled `Name@(Type, ...)` (arguments after the name, so nested arguments read left to right) and `@inst Name` (every parameter defaulted); `@inst(Type, ...) Name` puts the arguments before the name as `@template(...)` does.
- Added: `@template` constant parameters may be `float`, `double` or a pointer. A float argument names the instance by its shortest decimal (`Gain__1p5`, `Gain__n0p25`, `Gain__1em05`); a pointer takes a function, the address of a file-scope or `static` object, or null (`before__int__cmp_int`, `before__int__null`), and inside the body each is a `const` object. NaN, `long double`, string literals and element addresses are errors.
- Added: `@specialize(Type, ...) struct Name { ... };` gives one argument list its own hand-written definition (struct, union, enum, typedef or function) in place of the template's body. The primary template comes first, a specialization comes before the list's first `@inst`, and the body is plain C: it does not see the parameter names or the primary's attributes.
- Changed: naming a template without instantiating it, such as `Vec @inst(int) a` or a file-scope `Vec g;`, reports `'Vec' is a template; instantiate it as 'Vec@(args)' or '@inst(args) Vec'` instead of `undefined variable`.
- Changed: a repeated `@inst` of an existing instance parses each constant parameter's type once per template and builds the instance name without allocating, so a template-heavy file compiles faster and in less memory.
- Fixed: a function `@template` may have a parenthesised name (`@template(T) T (fn)(T x) {...}`), a return type written `@inst(T) Vec` or `_Atomic(T)`, or return a function pointer; each was rejected as not a definition.
- Fixed: a template struct, union or enum may carry an attribute between the keyword and the tag or after the closing brace (`@template(T) struct __attribute__((aligned(16))) F {...}`, `struct G {...} __attribute__((aligned(16)));`); the instance was left untagged and incomplete, or rejected as declaring variables. `@specialize` takes the same positions.
- Fixed: `enum __attribute__((unused)) E {...}`, `enum [[gnu::unused]] E {...}` and `enum E {...} __attribute__((unused))` are accepted in plain C.
- Changed: `aligned`, `packed`, `cleanup`, `vector_size`, `gnu_inline`, `constructor` and `destructor` are accepted in every spelling: `[[aligned(16)]]`, `[[cccc::packed]]`, `[[gnu::aligned(16)]]`, `__attribute__((__packed__))`. `[[aligned(16)]]`, `[[cccc::aligned(16)]]` and the `__aligned__` forms were silently ignored.
- Changed: the `__name__` spelling works inside `[[...]]` for `nonnull`, `returns_nonnull`, `sentinel`, `alloc_size`, `malloc`, `designated_init`, `optimize` and the checked-pointer attributes (`[[__nonnull__]]`, `[[gnu::__sentinel__]]`); each was silently ignored.
- Changed: an attribute in any vendor namespace resolves by its name (`[[clang::aligned(16)]]`, `[[acme::packed]]`), and an unknown namespaced attribute is a `-Wattributes` warning (`unknown attribute 'foo::bar' ignored`) instead of a parse error. `[[kernel]]`, `[[trusted]]`, `[[operator]]` and `[[method]]` work without the `cccc::` prefix; a bare `[[kernel]]` now runs the kernel-code check. The OpenCL address-space names (`global`, `local`, `constant`, `private`, `generic`) still need `cccc::`.
- Changed: an attribute behaves the same in `[[...]]` and `__attribute__((...))`. `[[gnu::warn_unused_result]]`, `[[gnu::format(...)]]`, `[[gnu::error("msg")]]`, `[[gnu::warning("msg")]]` and `[[gnu::unused]]` now take effect, and `__attribute__((nodiscard))`, `((kernel))`, `((trusted))`, `((operator(+)))` and `((method(T)))` are recognised; each was ignored in that spelling.

## [0.33.2] - 2026-11-09

- Fixed: deeply nested expressions no longer overflow the compiler's native stack, which crashed `cccc` built with AddressSanitizer; code generation runs on a larger stack.

## [0.33.1] - 2026-11-09

- Fixed: a member of a call result, such as `f().x`, as an asm memory operand under `--asm-passthru` is a compile error ("not addressable") instead of being passed to the host.
- Fixed: a function type-name written inline, such as `int(void)` or `void(const int)`, parses as an operand of `__builtin_types_compatible_p`, `sizeof` and casts.
- Added: comptime code reads the details of goto, break, continue and label nodes (`LabelName`, `GotoJumpKind`), inline asm (`AsmTemplate`, `AsmOperandConstraint`, `AsmOperandName`, `AsmOperandExpr`, `AsmClobberAt`, `AsmIsVolatile`, `AsmIsGoto`, `AsmGotoLabelAt` and the counts), atomics and fences (`AtomicOrder`, `AtomicFailOrder`, `AtomicIsWeak`, `CasAddr`, `CasExpected`, `CasDesired`, `FenceIsSignal`), and bit operations and checked arithmetic (`BitopOp`, `BitopWidth`, `OverflowOp`, `OverflowResultPtr`).

## [0.33.0] - 2026-11-08

- Added: comptime code reads the typed AST of a function definition: `FnBody`, `FnParamAt`, `FnLocalAt`, `GetNodeKind`, `GetNodeType`, operand accessors (`NodeLhs`, `NodeRhs`, `NodeCond`, `NodeThen`, `NodeElse`, `NodeInit`, `NodeInc`), `BlockAt`, `CallCallee`/`CallArgAt`, `NodeIntValue`/`NodeFloatValue`, `NodeVarObj`, `NodeMember`, switch and case accessors, `MacroCallName`/`MacroCallArgAt` for comptime calls not yet expanded, and `StripImplicit`.
- Changed: `NodeKind` has an `NK_*` constant for every node kind (`NK_GOTO`, `NK_LABEL`, `NK_STMT_EXPR`, `NK_ASM` and the rest).

## [0.32.1] - 2026-11-08

- Added: a struct or union `return =` assertion accepts a typedef name as the compound literal type: `return = (Pt){.x = 3, .y = 4}`.
- Changed: the type of a `return =` compound literal, `(struct TAG){...}` as well as `(TYPEDEF){...}`, and of each literal nested in it, must match the return type or the member or element it initialises; a mismatch is a compile error.
- Changed: a compound-literal `return =` on a function that does not return a struct or union is a compile error instead of a runtime "(type mismatch)" failure.
- Added: the type of a `return =` compound literal may carry `const`, `volatile`, `_Atomic` or a C23 storage-class specifier (`static`, `constexpr`, `register`, `thread_local`), before or after the type: `return = (const Pt){.x = 3, .y = 4}`.
- Changed: `restrict` or the `_Atomic(TYPE)` specifier in a `return =` compound literal type is a compile error that names it instead of a misleading "not a constant expression" error.
- Changed: a designator in a `return =` compound literal that names no member of its type is a compile error instead of an entry that is never compared.
- Changed: a call that leaves out a parameter whose `@default` names an earlier parameter reads a literal argument, or a plain local argument in a call with no side effects, directly instead of through a temporary: `box(10)` is `box(10, 10 * 2)` in `-c=native` output.
- Fixed: under `--testing=native`, a struct or union `return =` assertion is checked field by field as in the VM instead of always passing.
- Fixed: a struct `return =` assertion reads bit-field members by value and `long double` members as floating point instead of comparing the raw storage.
- Added: `return =` asserts pointers: `NULL`, `nullptr` or `0` for a null pointer field or return value, and `.field != value` (any scalar field in a compound literal) or `return != NULL` for non-null. A non-null pointer constant is a compile error.
- Changed: a struct `return =` with an ordered comparison, a malformed or too deeply nested compound literal, and `exit_code =` together with `return =` are compile errors instead of `-Wattributes` warnings that skipped the assertion.
- Fixed: under `--testing=native`, a float `return =` assertion applies its operator (`!=`, `<`, `<=`, `>`, `>=`) as the VM does instead of always testing equality.
- Changed: `[[cccc::test(return = ...)]]` takes any integer or floating constant expression, so enum names, `#define` names, casts, `sizeof` and arithmetic work, as struct field values do. An undeclared or non-constant operand is a compile error instead of a skipped assertion that always passed.
- Fixed: `return = 0xBEEF` is no longer read as a float, `return = 'A'` asserts 65 instead of 0, and `return = -1.5` asserts a float instead of the integer -1.
- Fixed: on Linux, a struct `return =` assertion on a `long double` member reads the value the test stored instead of always seeing 0.

## [0.32.0] - 2026-11-08

- Added: a parameter's `@default` can name an earlier parameter (`int box(int w, int h @default(w * 2))`; `box(10)` is `box(10, 20)`). The call stores the arguments up to the last one a default names in temporaries, left to right, so each is evaluated once; a keyed or defaulted earlier argument counts as its resolved value, and a method receiver can be named. The temporaries are plain C under `-c=native`.
- Changed: a `@default` that names its own parameter or a later one (`int f(int a @default(a))` with a global `a`) is an error instead of binding the global, and one that uses a constant declared in the parameter list (`int f(enum { A, B } v @default(B))`) is an error at the declaration instead of at every call.
- Changed: a `@default` cannot name a checked pointer parameter, and an argument for a checked pointer parameter stays in the call so its bounds are still verified.
- Changed: `long long` is a distinct type from `long`: `int f(long); int f(long long);` is a conflicting declaration, and `_Generic`, integer conversion rank and `LL`/`ULL` literals tell them apart.
- Changed: a conflicting redeclaration of an object (`int x; long x;`) and of a function, including one that conflicts with a block-scope declaration, is an error; a leading `...` (`int f(...)`) is an error before C23.
- Fixed: C23 `va_start(ap)` with one argument works, a `va_start` argument after the first one is ignored, and a `va_start` in a function that is not variadic is reported at the call.
- Fixed: a prototype after an unprototyped `()` declaration becomes the function's type, so later calls convert to its parameter types; a block-scope prototype's type applies only inside its block.
- Fixed: a call through an unprototyped declaration passes floating-point arguments with the fixed-parameter convention, and `-c=native` and `-m` print the type as `()` and cast the callee to its argument types when they differ from the prototype.
- Fixed: a K&R definition takes `float`, `char` and `short` parameters at their promoted type in `-c=native` and `-m` output, converts a `_Bool` parameter on entry in the VM, and accepts several declarators in one parameter declaration (`int a, b;`).
- Fixed: structs with the same tag from separate input files, such as a complete type from a comptime-generated function and an opaque one in a host file, are compatible instead of reporting conflicting types.
- Fixed: a diagnostic inside a macro body is reported at the invocation with an `expanded from macro` note.
- Fixed: `--watch` events for the watched directory itself are skipped, the FSEvents event list is locked, and files with the same name in different watches no longer merge into one event.
- Fixed: the `ccccl` `-c=generated` step passes `--comptime-allow=fs`, and the `make` build compiles without three warnings.

## [0.31.2] - 2026-11-06

- Fixed: a block call converts each argument to its parameter's type (`int (^b)(int)` called with `42.9` passes `42`) and reports too few or too many arguments, like a function call.
- Fixed: a `@comptime` function call with too few or too many arguments is an error.
- Fixed: `blk(.a = 1)` on a block reports "keyed arguments need a direct call to a function", `f(.a = 1)` on a `@comptime` function reports "keyed arguments do not apply to a @comptime function", and `@default` on a `@comptime` parameter reports "@default does not apply to a parameter of a @comptime function".
- Fixed: a block literal can be variadic (`^(int n, ...)`), reading its tail with `<stdarg.h>`; a block literal needs a named parameter before `...`.
- Fixed: a block literal with an explicit return type and parameters (`^double(int n) { ... }`) parses.

## [0.31.1] - 2026-11-05

- Fixed: a function built by a macro (`FunctionAddParam`), such as an `@ctor` `T_new`, accepts keyed arguments: `Win_new(.d = 2, .w = 1)`.
- Fixed: `f(.a = 3, )` reports "expected an expression" instead of "positional argument after a keyed argument", and "too few arguments" names the first parameter with neither an argument nor a default.
- Fixed: the smoke tests skip a host compiler that cannot build a program (a Homebrew llvm clang with no matching SDK), and the release workflow's hang-watchdog trap no longer fails a passing job.

## [0.31.0] - 2026-11-05

- Added: a custom attribute can sit on a parameter of a function declared at file scope (`int area(int w @default(80), int h @default(24));`). Its handler runs with an `ATTR_TARGET_PARAM` target (`AttrTargetParam`, `AttrTargetParamIndex`), and `ParamHasAttr`, `ParamAttrArg` and five more accessors read the attributes from the parameter `Type *`. A parameter's attribute is written on one declaration only and the definition inherits it. `@default` accepts a parameter, and every parameter after a defaulted one needs a default.
- Added: a call that leaves out trailing arguments takes the parameters' `@default` values (`area()` is `area(80, 24)`), and `f(.name = v)` gives an argument by parameter name, after the positional ones (`area(.h = 10)` is `area(80, 10)`). Defaults and keys apply to direct calls and method calls, not calls through function pointers; a default cannot name another parameter.
- Fixed: a checked access keeps its range check across a macro call that has not been expanded: a loop holding one is not treated as having a provable index, and a macro call in a loop condition forgets the `break`/`continue` state.

## [0.30.0] - 2026-11-04

- Changed: a checked access whose identical check already ran on every path to it skips the range check across `if`/`else` joins, in loop bodies and conditions, past `break`/`continue` and through `switch` cases, not only within one straight-line run. A kernel with branches and loops over `p[i]` goes from 7 range checks to 1 and runs 1.5x faster; the benchmark programs do not use `--checked-pointers` and are unchanged.
- Changed: a label no `goto` or asm goto names no longer stops a counted loop's index from being proven in range, or ends a run of dominated checks.
- Changed: the compile-time reflect operator is `^^name` instead of `$name`. `$` is an ordinary identifier character again outside quasi-quote templates, so `int $x;` compiles.
- Fixed: `-c=native` wrote an enumerator of `0x8000000000000000` in an enum with a 64-bit unsigned underlying type as `-9223372036854775808`, which gcc rejects; it is now written as `9223372036854775808ULL`, and a plain `LLONG_MIN` enumerator as `(-9223372036854775807LL - 1)`.
- Added: a member initializer in a struct body (`int n = 3;`) is sugar for `@default(3)` through the `@ctor` constructor; it needs `@ctor` on the struct and rejects array members and a member that also has `@default` or `@ctor_skip`.
- Fixed: a custom attribute on a tag defined with variables (`struct S { ... } @attr g;`) runs once as a type target where the tag is defined, and a typedef's attribute runs once instead of again for each variable of that type; an attribute handler's note points at the `@name` source line.
- Fixed: `calloc` overflow returns `NULL` with `ENOMEM`, a sign-extending shift and negating `INT_MIN` use unsigned arithmetic, and the NSS shims copy unaligned pointers with `memcpy`, so a UBSan build reports nothing.
- Changed: `cccc -m` over cccc's own sources is faster: the preprocessor memoizes canonical include paths, and the serializer memoizes type comparisons proven under a co-inductive assumption.

## [0.29.0] - 2026-11-01

- Changed: against 0.28.0 the benchmark programs run 1.4x faster on the geometric mean (278 ms against 396 ms; `sieve` 2.4x, `dispatch` 2.3x, `nqueens` 1.7x, none slower), and `cccc -m` over cccc's own 103 sources takes 18 s and 3.4 GB instead of 149 s and 19 GB.
- Fixed: a bit-field store at bit 63 (`long long y : 1` after 62 bits) shifted a signed value into the sign bit when the optimiser is off or a safety flag is set.
- Changed: the `-c=native` serializer skips the type definitions of a function that owns no local type, looks up the function Objs a shim needs by name instead of scanning the program for each, and resolves a type's own typedef or tag record by pointer; `cccc -m` over cccc's 103 sources takes 19 s instead of 29 s (45 s before the previous change).
- Changed: the `-c=native` serializer's rename passes (tag, tagless-typedef and enumerator collisions, and a program typedef shadowing a header one) compare a type only with the groups and records that can be the same type, and the type-comparison memo is a flat table; serializing cccc's own 103 sources with `-m` takes 29 s instead of 45 s.
- Added: VM opcodes `UBFX`, `SBFX` and `BFI` (bit-field extract and insert). A bit-field read, `(x >> 8) & 0xff` and `(x << 44) >> 52` are one extract opcode, and a bit-field write is one insert opcode instead of four to six (`minilua` runs Lua programs 2-3% faster). They are not used under any flag that checks or instruments the access.
- Changed: the VM bytecode clean-up turns an immediate op that leaves its operand unchanged (`ADDI3`, `ORI3`, `XORI3`, `SHLI3`, `SHRI3`, `USHRI3` by 0, `ANDI3` by -1, `MULI3` by 1) into a `MOV3`, and `ANDI3` / `MULI3` by 0 into a `MOV3` from the zero register.
- Fixed: `NEG3` of the most negative `long long` and a register shift by a count of 64 or more, or a negative count, no longer invoke undefined behaviour in the VM host; the count is taken modulo 64.
- Changed: the VM bytecode clean-up knows a logical not (`NOT3`) is 0 or 1, a divide by a positive value and a bitwise not stay within their operand's range, and fuses a negation with the wrap after it into `SUBSX3` / `SUBZX3` from the zero register, so none of them emit a separate `SX4` / `ZX4`.
- Changed: the VM bytecode clean-up knows an unsigned shift right, divide or remainder of a value already known non-negative stays within the source's range, so converting the result back to `unsigned` no longer emits a `ZX4` after `USHRI3`, `USHR3`, `UDIV3` or `UMOD3`.
- Changed: the VM bytecode clean-up knows a halfword load (`LDR_H`, `LDR_LOCAL_H`, `LDR_INDEX_H`) is already sign-extended, so widening a `short` to `int` or `long` no longer emits an `SX2` / `SX4` after it.
- Changed: a load of an `unsigned char`, `_Bool` or `unsigned` is one zero-extending VM opcode instead of a sign-extending load followed by `ZX1` / `ZX4`: new opcodes `LDR_UB`, `LDR_UW`, `LDR_LOCAL_UB`, `LDR_LOCAL_UW`, `LDR_INDEX_UB` and `LDR_INDEX_UW`.
- Changed: the VM bytecode clean-up drops the sign or zero extension after a 32-bit `int` / `unsigned` operation whose upper half nothing reads (a 4-byte store, another extension, another 32-bit operation), and fuses the extension that remains into the operation: new opcodes `ADDSX3`, `ADDZX3`, `SUBSX3`, `SUBZX3`, `MULSX3`, `MULZX3`, `ADDISX3`, `ADDIZX3`, `MULISX3`, `MULIZX3`, `SHLISX3` and `SHLIZX3`.
- Added: VM opcodes `ORI3` and `XORI3` (OR or XOR by an immediate), so `x | 0xf0` and `x ^ 0xff` are one instruction instead of a `LI3` and an `OR3` / `XOR3`.
- Changed: a compare against a constant beyond 24 bits (a loop bound of `10000000`, `x != 0xFFFFFFFF`) tested by the branch right after it is one compare-and-branch opcode with a 56-bit immediate (`BLTW3`, `BNEW3`, ... 10 in all).
- Fixed: the clean-up pass checked the bytecode of a function only when it had rewritten something other than a branch fusion; a function changed only by fusion is now checked too.
- Changed: the VM bytecode clean-up narrows a loop counter's interval back to its bounds after widening it around the loop, so a test inside `for (i = 0; i < 5; i++)` that the bounds decide (`i < 10`) is folded.
- Added: VM compare-and-branch opcodes (`BEQ3`, `BLT3`, `BGEI3`, ... 16 in all): an integer compare tested by a branch is one instruction instead of a compare and a `JZ3` / `JNZ3`.
- Fixed: `--vm-profile` printed no report when the program called `exit()`; the report is written after the `atexit` handlers and destructors run (`_Exit()`, `quick_exit()` and `abort()` still end the run without one).
- Changed: the VM bytecode clean-up folds a comparison whose operand intervals decide it, folds or removes a branch on a known value, and deletes code no path reaches.
- Fixed: a `return` built by a macro or template (`@from_string` on an enum returning `-1LL`) is converted to the function's integer return type, so callers see a value normalised to it.
- Changed: the VM bytecode clean-up folds a constant that one operation reads into that operation (`LI3 t, c; SLT3 d, a, t` becomes `SLTI3 d, a, c`, a `switch`'s comparisons and a pointer stepped back by a constant), and a call reads only the argument and saved registers, so a temporary or result copy nothing reads afterwards is removed.
- Changed: the operand of an immediate-form operation may be any constant expression, not only a literal (`x & (1 + 2)`, `x < -(1 << 4)`).
- Changed: the VM bytecode clean-up keeps no sign or zero extension after a direct call to a function whose declared return type is `int`, `unsigned`, `short`, `char` or `_Bool` (`int p = partition(lo, hi)`), since the callee returns the value normalised.
- Fixed: a tail call `return g(x);` of a wider value to a `char`-, `short`- or `int`-sized enum return type skipped the narrowing, so the caller saw the wider value.
- Changed: a `for` or `while` loop with a small scalar condition tests it at the bottom and branches back into the body, so each iteration runs one branch instead of a test and a `JMP`.
- Fixed: a `float`, `double` or `_Complex` value used as a truth value (`if (d)`, `!d`, `d && x`, `(_Bool)d`, a `_Bool` initializer or store) tested the wrong register or truncated to an integer; it is now true when nonzero, NaN included, in the VM, in constant expressions and under `-c=native`. New VM opcodes `FNZ3` and `FNZ3_F32`.
- Added: VM opcodes `SHLI3`, `SHRI3`, `USHRI3` and `ANDI3` (shift or AND by an immediate); a multiplication by a constant outside the `int` range also takes an immediate (`dispatch.c` runs ~6% fewer VM ops).
- Changed: `&&`, `||` and `!` in an `if`, loop or `?:` condition jump straight to the branch targets instead of building a 0/1 value and testing it again (`mandelbrot.c` runs ~12% fewer VM ops, `nqueens.c` ~11%).
- Changed: loop locals are promoted to registers in a function whose loop is a backward `goto` or a computed-goto dispatcher (`dispatch.c` runs ~25% faster).
- Changed: the VM bytecode clean-up replaces an operation on known constants by its result when it fits in place, turns `x + 0`, `x * 1` and `x & -1` into copies, a constant-index `MULADDI3` into `ADDI3`, and reads a register loaded with 0 as the zero register.
- Added: build API `AddSystemInclude`, `AddFrameworkPath` and `UseSystemHeaders` (`CcccExecutable` and `CcccObject` only).
- Added: the `ouroboros` build target builds cccc with `cccc -c=native` and runs the result's `--version`.
- Added: Lua ports of the benchmark programs in `tests/benchmarks/lua/` and a `lua` column in `tools/bench.py`.
- Changed: `tools/bench.py` compares against `gcc -O0` and `-O2`; the `bench_compare*` build targets run the release binary.
- Fixed: a command-line `-i DIR` default reached a `CcccExecutable` as `-isystem DIR`, which `cccc` read as `-i system`.
- Fixed: `ExcludeSource` had no effect on a `CcccExecutable` target.
- Fixed: `-c=native` of cccc's own sources failed with a duplicate `__cccc_native_strtol` runtime.
- Added: Linux `O_PATH` in `<fcntl.h>` and `syscall()` in `<unistd.h>` (declared and registered under `__linux__`; comptime class `exec`).
- Fixed: `<sys/time.h>` now makes `fd_set` and the `FD_*` macros visible.
- Fixed: an `#include_next` that finds no further copy of a standard header uses the bundled one, so glibc's `<limits.h>` works under `--use-system-headers` on Linux.
- Fixed: `__restrict` and `__restrict__` are accepted inside an array parameter's brackets (`int a[__restrict 4]`).
- Added: the GNU predefined identifier `__PRETTY_FUNCTION__`.
- Changed: the `ouroboros` target uses the system headers on Linux, as on macOS.
- Changed: `Type`, `Node`, `Obj` and `Token` keep their rarely used fields in lazily allocated groups, so they shrink from 456, 808, 752 and 176 bytes to 128, 168, 120 and 96, and the front end recycles the tokens the preprocessor drops; compiling cccc's own sources needs far less memory.
- Fixed: a `case` label keeps all 64 bits, and an unsigned 64-bit `switch` is ordered as unsigned, so two labels beyond 32 bits no longer collide.
- Fixed: a bound operator under `-c=native` evaluates its left operand first when an operand has side effects, where the host compiler may have evaluated them right to left.
- Fixed: an uninitialized bit-field no longer drops the later members of a static initializer.
- Fixed: `-c=object` leaves a static initializer's address of an undefined function to the linker.
- Fixed: `--sandbox` on Linux refuses a raw `syscall()` that opens a socket through `io_uring`, an x32-ABI number or a foreign-architecture syscall.
- Fixed: the `--watch` inotify thread no longer blocks in `read()` after an interrupted `select()`, which deadlocked the next `dmon_watch()`.
- Fixed: `-c=native` on Linux undefines glibc's member-alias macros (`sa_handler`, `si_status`) before the type definitions, and keeps a prototype written in an input file (`mremap`, `fallocate`, `splice`).

## [0.28.0] - 2026-10-29

- Added: the VM fuses `*(base + idx*k + c)` into one indexed load or store opcode, and `x + a*b`, `a*K` and floating-point `c + a*b` into multiply-add opcodes. Fusion is off under any pointer, type, overflow, uninitialised-read or instrumentation check.
- Added: a function with loops keeps up to four integer and four floating-point locals used inside a loop in callee-saved registers, and reads them directly as instruction operands. A local whose address is taken, that a nested function captures, or that has a `cleanup` attribute, and every local of a function that calls `setjmp`, stay in memory. `setjmp` saves these registers in the `jmp_buf` and `longjmp` restores them.
- Added: an always-on constant folding, immediate-operand, copy-propagation, dead-definition and redundant-extension pass over each function's bytecode. `x++` and `x op= y` on a plain variable lower to `x = x op y`. Extension facts carry across branches, switch tables and computed gotos; functions with `setjmp`, computed goto or `cleanup` variables get the same clean-up. All of it is off under the flags that check or instrument locals, and under an explicit `-g`.
- Added: an extern data object defined by a `-l` library binds through `dlsym`.
- Added: pre-C23 `-Wpedantic` names each for-init declarator and warns on a tag declared there.
- Added: `-c=native` stores a pointer in a union or packed slot of a wide-`_BitInt` bit-field aggregate from a startup constructor.
- Fixed: postfix `++` and `--` on a `_Bool` yield the old value.
- Fixed: the wide `printf` family copies the format's terminating NUL.
- Fixed: `make` rebuilds `./cccc` after an edit to `src/ops.c` or a `src` header.
- Fixed: `test_msan` no longer reports libffi's uninstrumented return, host-call out-parameters or callback arguments as uninitialised.

## [0.27.0] - 2026-10-28

- Fixed: a `longjmp` out of a guest callback (a `qsort`/`bsearch`/`tsearch` comparator, a `glob`/`scandir`/`fts` callback, an obstack handler) leaves the native frames and lands in the `setjmp` frame, as in compiled C.
- Fixed: `strtol`, `strtoll`, `strtoul` and `strtoull` accept the C23 `0b` prefix under `--use-system-headers -c=native`.
- Added: the VM implements every remaining `__*_chk` entry point glibc 2.39's fortified headers reference: wide-character (`__wmemcpy_chk`, `__wcscpy_chk`, `__swprintf_chk`, ...), `__poll_chk`, `__ppoll_chk`, `__syslog_chk`, `__asprintf_chk`, `__dprintf_chk`, `__realpath_chk`, `__mbstowcs_chk`, `__wctomb_chk`, `__getgroups_chk`, `__readlinkat_chk` and the rest. A failed size check aborts (exit 134). Only the obstack pair is left out.
- Added: libc functions the fortified headers reach, as VM natives: `strlcpy`, `strlcat`, `dprintf`, `vdprintf`, `readlinkat`, `getdomainname`, `getwd`, `ptsname_r`, `wmemcpy`, `wmemmove`, `wmemset`, `wcpcpy`, `wcpncpy`, `wcslcpy`, `wcslcat`, `fgetws`, `mbsnrtowcs`, `wcsnrtombs` and the wide printf family; on Linux also `explicit_bzero`, `fgets_unlocked`, `fread_unlocked`, `wmempcpy` and `fgetws_unlocked`.
- Added: the library bounds table covers the new byte-counted entry points.
- Added: a row of a row pointer (`p[0]`, `*p`, `p[0][1]`) passed to a checked parameter is verified against the row's extent, also when `p` is checked-rooted on only some paths.
- Added: a count written inside a used value is rechecked when `&&`, `||`, `?:` or `,` sequences it before a call that ends tracking (`if ((s.n = 8) && fill(&s))`), outside a loop.
- Added: a count written earlier in a nested statement than the call that ends tracking is rechecked (`if ((s.n = 8)) { fill(&s); }`, `if (c) { s.n = 8; } else { fill(&s); }`), unless a loop contains both.

## [0.26.0] - 2026-10-27

- Added: `__builtin___memcpy_chk`, `__builtin___snprintf_chk` and the rest of the `__builtin___*_chk` family, with `__has_builtin` support. Apple's SDK now routes its fortified `memcpy`/`strcpy`/`snprintf` calls to them, so `-D_FORTIFY_SOURCE=0` is not needed to compile programs against it. A size check that fails aborts with `*** buffer overflow detected ***` (exit 134).
- Added: the VM implements glibc's `__memcpy_chk`, `__read_chk`, `__snprintf_chk`, `__fgets_chk`, `__recv_chk` and related entry points, and the library bounds table covers them, so fortified glibc headers (`-O1 -D_FORTIFY_SOURCE=2`) work under `--checked-pointers`.
- Added: `-O1` and above define `__OPTIMIZE__`.
- Added: `__typeof` is accepted as a spelling of `typeof` (glibc's `<sys/cdefs.h>`).
- Added: library bounds for `strncpy`, `stpncpy`, `strlcpy` and `strlcat` (destination).
- Fixed: `__builtin_memcpy`, `__builtin_memset` and `__builtin_memmove` are bounds-checked under `--checked-pointers`.
- Added: a checked flexible array member takes `byte_count(n)`, `bounds(lo, hi)` and `bounds(unknown)` as well as `count(n)`, in `[[cccc::...]]` or `__attribute__((...))` spelling.
- Fixed: a bit-field store into an `_Nt_checked` terminator slot traps when the value truncated to the field width is non-zero (`b[n].f = 1`).
- Added: an unchecked local assigned from a plain array (`int *q = buf;`) carries the array's extent into checked targets (`s.p = q`, `sink(q, n)`, `return q`); accesses through the local stay unchecked.
- Added: `&buf[i]`, a row of a multidimensional plain array (`m[i]`, bounded by the array it indexes) and the last member of a complete struct object (`s.data`) are plain-array sources for checked targets.
- Added: a member array of an element of an array of structs or unions (`arr[k].data`, bounded by the member, empty when an index is outside the array it indexes) and a union's array member on a complete object (`u.a`) are plain-array sources for checked targets.
- Added: a count written inside a value that is used (`if ((s.n = f()))`, `x = (s.n = 8)`, `return s.n = 8`) is rechecked against the pointer's source range and keeps its value.
- Added: a count written after other statements, in a nested `if`/loop, or through a local pointer to the struct is checked against the source range the checked pointer was stored from (`s.p = small; log(); s.n = 8;` traps at the write).
- Added: any dimension of a variable-length checked array may vary (`_Checked[3][n]`, `_Checked[m][n]`), as a local, a parameter or behind a row pointer; each is checked against its own length.
- Fixed: a function parameter whose pointee is variable-length (`int (*a)[n]`, `int a[][n]`) no longer crashes the compiler when subscripted; its lengths are evaluated once on entry.
- Fixed: `-c=native` spells a parameter whose pointee is variable-length (`int (*a)[n]`, `int a[][n]`): `[*]` in prototypes, the parameter names in the definition, and `void *` at call sites.
- Added: `[*]` (unspecified extent) is accepted in a prototype's parameters and rejected in a definition.
- Fixed: `-c=native` evaluates a parameter extent with side effects (`int (*a)[g()]`) once per call, as the VM does.
- Fixed: `-c=native` declares a checked pointer to a variable-length row (`int (*p)[n] = a;`) before its bounds are used.
- Fixed: a call is bounds-checked when a bound names a struct, union or floating parameter, for both an argument bound and a checked-return call.
- Fixed: a call is bounds-checked when a checked pointer argument that another parameter's bound names has side effects, or comes before the pointer that names it (`sink(pk, small)` with `count(*p)`).
- Added: a call to a function with a checked return type carries the declared return bounds, with the call's arguments substituted. A dereference, a propagated local, a checked assignment, an annotated argument, a checked return and a `dynamic` cast all accept it. A `return` is verified against the parameters' values at entry.
- Changed: a bound on a function's return type that names an undeclared identifier is an error at the declaration, in every mode.
- Changed: a checked region rejects a plain array declaration or array compound literal; write `_Checked[N]`/`_Nt_checked[N]`. Array typedefs from headers (`jmp_buf`) are exempt.
- Added: checked flexible array members, `int data _Checked[] [[cccc::count(n)]];` (also `_Nt_checked[]`), bounds-checked against a sibling field.
- Added: `_Checked`/`_Nt_checked` in an abstract declarator, so a compound literal such as `(int _Checked[4]){1, 2, 3, 4}` is a bounds-checked array.
- Fixed: `char s _Nt_checked[] = "hi"` takes its extent from the string initializer instead of failing with "must declare at least 1 element".
- Added: variable-length checked arrays (`int a _Checked[n]`, `_Nt_checked[n]`, and `_Checked[n]` parameters). A local is checked against the length computed at its declaration; a parameter is `count(n)`. Only the outermost dimension may be variable.
- Fixed: under the VM heap, `realloc`, `calloc`, `reallocarray`, `aligned_alloc`, `posix_memalign` and `free_sized` no longer lose an earlier argument when a later one contains a call (`realloc(p, n * f())` aborted).
- Added: `@deserialize` publishes `int T_deserialize_into(T *out, const void *buf, unsigned long len)`, a bounds-checked read that returns nonzero on a malformed buffer and leaves `*out` untouched.
- Added: `@serialize` and `@deserialize` write and read the content of `@owned` members after the fixed bytes. `@serialize` also publishes `T_serialize_size`, and `@deserialize` publishes `T_deserialize_read`. Structs without `@owned` members serialize as before.
- Fixed: `@serialize`, `@deserialize`, `@equals`, `@hash`, `@compare`, `@to_json`, `@from_json`, `@dump`, `@to_string` and `@from_string` handle an `@owned` member whose pointee struct is declared later (single object or `@owned("n")` array), stepping through it with `T_clone_size`.
- Added: `@from_string` on a struct publishes `int T_from_string(T *out, const char *s)`, which reads the text `@to_string` writes. It needs `#include <cccc/text.h>`; `*out` is written only on success.
- Changed: `@dump` and `@to_string` escape `"`, `\` and control bytes in strings and print a `long double` with 21 significant digits, so the text reads back exactly.
- Added: bundle attributes `@json`, `@text`, `@binary`, `@lifecycle`, `@hashable` and `@ordered`, each a pair of existing derives with one optional prefix.
- Fixed: the "expected unique generated function name" error names the attribute and its location.
- Changed: `@enum_to_string` and `@enum_from_string` are now `@to_string` and `@from_string`; the old names are an undefined-attribute error. Enum behaviour is unchanged, and both take an optional name prefix.
- Added: `@to_string` on a struct publishes `size_t T_to_string(const T *self, char *buf, size_t cap)`, writing the `@dump` text into a buffer (snprintf-style return). `@to_string_skip` leaves a member out of it; `@dump_skip` does too.
- Added: a checked return type is verified at `return`. The value must root in a checked pointer or a local that inherits its bounds, and its bounds must imply the declared ones, which may name parameters.
- Added: a `dynamic` bounds cast, a checked assignment and an annotated call argument accept a local that inherits its bounds (propagated from a checked pointer) as the source. A `dynamic` cast from a local whose bounds are known on only some paths is a compile error.
- Fixed: a `#pragma cccc checked`/`unchecked` `end` must name the same keyword as its `begin`.
- Added: `-Wchecked-regions`. Checked-region violations stay errors by default; `-Wno-error=checked-regions` or `#pragma GCC diagnostic warning "-Wchecked-regions"` turns them into warnings so a codebase can migrate piece by piece. `-Wno-checked-regions` does not silence them.
- Added: `-Wchecked-calls` (opt-in) and `[[cccc::trusted]]`. A checked region reports a call to a prototype with unchecked pointers and a checked pointer passed to a raw parameter; header declarations and trusted prototypes are exempt.

## [0.25.1] - 2026-10-25

- Fixed: `--overflow-checks` traps signed overflow at the width of the
  operation's type, so `INT_MAX + 1`, `INT_MIN / -1`, `x++` and signed
  `_BitInt(N)` of 64 bits or fewer report `INTEGER OVERFLOW`. Unary `-` is
  checked too, including `-LLONG_MIN`. See `docs/safety-overflow.md`.
- Fixed: results of `+ - * <<` and unary `-` and `~` on 32-bit `int`,
  `unsigned` and enums are wrapped to their width, so widening casts,
  conversion to float and `>>` no longer read stale high bits. `~` on a
  `_Complex` value is the conjugate.

## [0.25.0] - 2026-10-25

- Added: `[[cccc::operator(...)]]` binds `& | ^ << >>` and the unary `- + ~ !`
  for struct and union operands, `++` and `--` use the `T + int` and
  `T - int` functions, and `[[cccc::operator([])]]` binds `a[i]`. Compound
  `*=`, `/=` and `%=` on structs call the bound function. See
  `docs/operators.md`.
- Fixed: a pointer operand to `* / % & | ^ << >>` or unary `+ - ~`, and a
  floating-point or `_Complex` operand to `% & | ^ << >>` or unary `~`, is a
  located `invalid operands` error at parse time instead of compiling or
  failing late in code generation. `~z` on a `_Complex` value is rejected.

## [0.24.0] - 2026-10-24

- Added: `<=>` binds a function as a three-way comparison, and `a <=> b` is an
  expression between shift and relational: struct and union operands call the
  bound function, integers, floats and pointers give -1, 0 or 1. See
  `docs/operators.md`.
- Added: `@compare` binds `T_compare` as `<=>` for `(T, T)`, so `a < b`,
  `a <= b`, `a > b` and `a >= b` work on the struct. See
  `docs/derive-equality.md`.

## [0.23.0] - 2026-10-24

- Added: an operator written in a struct or union body is a function named
  `T_operator_<name>`. See `docs/operators.md`.
- Added: `_Static_assert` in a struct or union body.
- Added: `-c=native` copies a multi-dimensional variable-length array into a
  task.
- Added: `@equals` binds `T_equals` as `==` for `(T, T)`, and an unbound
  `a != b` is `!(a == b)`. See `docs/derive-equality.md`.
- Changed: operator and method lookup is indexed by the aggregate type.
- Fixed: comparing two structs or unions with no bound operator compiled and
  returned an arbitrary value; it is now `invalid operands to ==`.

## [0.22.1] - 2026-10-24

- Added: a function written in a struct or union body is a method named
  `T_f`, hoisted to file scope. See `docs/methods.md`.
- Changed: `s.f` naming a method without calling it is an error that says so.
- Fixed: a `static inline` function reached only through method or operator
  syntax was never emitted.

## [0.22.0] - 2026-10-24

- Added: `[[cccc::operator(sym)]]` binds `+ - * / %` and `== != < <= > >=` to
  a function for struct and union operands; `a op= b` uses the binary
  operator. See `docs/operators.md`.
- Added: `[[cccc::method(T)]]` and `[[cccc::method(T, name)]]` make a free
  function callable as `s.f(x)` and `p->f(x)`. See `docs/methods.md`.
- Fixed: an rvalue operand passed to a pointer parameter of an operator
  function is copied into a temporary. `mk() == b` compiled in the VM but
  failed under `-c=native`.

## [0.21.3] - 2026-10-23

- Changed: under `-c=native`, `-m` and `-c=generated`, `task` queues its block
  for the team instead of running it where it is met. Queued tasks run at a
  barrier, at `taskwait`, at the end of a `taskgroup`, at the end of a
  `parallel` region and on idle threads. `if`, `final`, `priority` and
  `depend` are honoured, and `omp_in_final()` is added. The VM still runs
  tasks in order.
- Changed: `depend(depobj: ...)` and the `iterator` modifier of `depend` are
  compile errors.
- Changed: under `-c=native`, a task can have its own copy of a
  one-dimensional variable-length array; a multi-dimensional one must be named
  in `shared`.
- Fixed: under `-c=native`, a nested function's local declared with an
  enclosing function's variable-length array type (`typeof` or a typedef) is
  emitted with its length instead of as `int x[]`.
- Fixed: under `-c=native`, a `parallel` region could hang when a smaller team
  ran between larger ones, because an idle pool thread read the finished
  team's size from a stack frame that had returned.

## [0.21.2] - 2026-10-23

- Added: `omp atomic` takes a memory order (`relaxed`, `acquire`, `release`,
  `acq_rel`, `seq_cst`) and `hint()`, plus the `compare`, `fail` and `weak`
  clauses and `capture` blocks. `-c=native` emits the order.
- Added: `atomic_compare_exchange_strong_explicit` and
  `atomic_compare_exchange_weak_explicit`.
- Changed: the `<stdatomic.h>` `_explicit` operations and weak compare-exchange
  pass a constant order and the weak flag to `-c=native` and `-m`; the VM stays
  sequentially consistent.
- Changed: under `-c=native`, `__atomic_fetch_*`, `__atomic_*_fetch` and the
  `<stdatomic.h>` fetch operations emit the single host builtin (one `ldadd` on
  arm64) for 1/2/4/8-byte integers and pointer add/sub, instead of a
  compare-exchange loop.
- Fixed: `atomic_fetch_add` and `atomic_fetch_sub` on an `_Atomic` pointer
  return the old pointer rather than a truncated `int`, and
  `__atomic_fetch_add` on an `int **` compiles under `-c=native`.

- Fixed: under `-c=native`, a `parallel` region inside a `task` gives the
  variables it uses the task's `firstprivate` copies instead of sharing the
  originals.
- Fixed: `-c=native`, `-m` and `-c=generated` emit a `goto` for a `break` or
  `continue` whose loop or switch is not the innermost one around it, instead of
  a keyword that bound to the wrong construct.
- Added: `-fopenmp` supports doacross loops: `ordered(n)` with
  `depend(sink: ...)`, `depend(source)` and the `doacross` spelling. Under
  `-c=native` a sink waits for the iteration it names.
- Added: `-Wopenmp` (on by default) for a `sink` that waits on the current or a
  later iteration, or on one the loop never runs; the sink is ignored.
- Fixed: under `-c=native`, `continue` in the body of `for` and `parallel for`
  is accepted instead of being a stray `continue`.
- Fixed: under `-c=native`, a doacross loop with `schedule(guided)` keeps one
  counter per chunk instead of one per iteration, and a `sink` waiting on a
  slow iteration sleeps instead of spinning.
- Changed: `break` out of the loop of `for` and `parallel for` is an error in
  the VM and under `-c=native`.

## [0.21.1] - 2026-10-23

- Fixed: under `-c=native`, `num_threads` and `if` expressions may use variables
  that `default(none)` on the same directive does not list.
- Changed: under `-c=native`, `sections` and `parallel sections` hand out one
  section at a time, so a free thread takes the next unclaimed block.

## [0.21.0] - 2026-10-23

- Added: `-fopenmp` runs `task`, `taskwait`, `taskgroup`, `taskyield`,
  `sections` and `parallel sections`. A task runs where it is met and works on
  its own copy of the locals it uses; native `sections` are split across the
  team.
- Added: `threadprivate`, `copyin` and `linear` (on `for`, `parallel for` and
  `simd`).
- Changed: `default(none)` is enforced, and `default` on a plain `omp for` is
  an error.
- Fixed: under `-c=native`, `firstprivate` with `lastprivate` on one variable
  could start a thread from the value another thread had written back.

## [0.20.1] - 2026-10-22

- Changed: a checked-pointer bound on a prototype, typedef, function pointer
  or struct member parameter is resolved where it is declared. A bound naming
  an undeclared identifier, or a global declared after the declaration, is an
  error there in every mode, instead of at the first call under
  `--checked-pointers`.
- Fixed: a checked-pointer bound on an unnamed prototype parameter, such as
  `void f(int *[[cccc::array, cccc::count(n)]], int n);`, is resolved and
  enforced at calls.

## [0.20.0] - 2026-10-22

- Changed: the guides in `docs/` are lowercase-kebab pages of about 100–400
  lines. The large topics (macros, safety, testing, attributes, build, derive,
  debugger, headers) are a hub page plus `<topic>-*.md` sub-pages, and
  `docs/README.md` indexes them all. `docs/getting-started.md` and
  `docs/features.md` are new. `testing.md` replaces `TEST_MODE.md`, `build.md`
  replaces `BUILD_MODE.md` and `install.md` replaces `BUILD.md`.
- Changed: the README is generated from the docs index, getting-started,
  features and tested examples in `examples/readme/`.
- Fixed: a comptime function pointer that comptime code stores in a global,
  static, struct member or array element stays valid across debugger `reload`,
  `fix` and `--watch` expansions.
- Fixed: a debugger `reload` or `--watch` save no longer leaks the parser scopes
  of a rebuilt comptime program, a refused parse or the comptime code it
  generates.

## [0.19.5] - 2026-10-22

- Added: debugger `reload` and `fix` expand calls to comptime functions in a
  live session, including from `--watch`. A reload also accepts changed or new
  comptime functions and variables: matching variables and statics keep their
  values, and the file's functions that call comptime code expand again.
  File-scope comptime calls in the reloaded file run again, and the
  functions they generate are redefined.
- Fixed: a `fix` or `reload` whose source ends inside a call's arguments is
  reported as an error instead of exiting the process.

## [0.19.4] - 2026-10-22

- Fixed: mutex and condition handles a guest never destroys are freed at
  exit, so threaded programs are clean under valgrind and `leaks`.
- Fixed: comptime `malloc`/`calloc`/`realloc` use the VM heap under `-V`
  (a `strcpy` into the result no longer faults).
- Tests: leak skips lifted from the pthread and comptime tests.

## [0.19.3] - 2026-10-21

- Fixed: the compiler frees its exit-time state (include and framework
  paths, serialize tables, file-scope asm and comptime records, the builder
  root), so a run is clean under valgrind and `leaks`.
- Fixed: assigning an anonymous aggregate to itself no longer overlaps its
  own copy.
- Added: `--test-run` runs `cccc_launch` in the VM smoke phase.
- Tests: `test_leaks` target runs every suite and loose test under `leaks`
  (macOS) or valgrind (Linux); comptime native smoke checks ported to loose
  tests and `tests/smoke` scripts; `test_suite_c23` runs under `-c=native`.

## [0.19.2] - 2026-10-20

- Fixed: threaded programs reclaim `alloca`/VLA storage at block and frame
  exit (reclamation previously stopped once any thread was created). Each
  thread reclaims only its own storage, and a thread ending through
  `pthread_exit`/`thrd_exit` has its outstanding storage reclaimed.
- Fixed: `__builtin_object_size` and `__builtin_dynamic_object_size` types
  1 and 3 bound the answer by the member the pointer names (`p->buf`,
  `&p->in.x`, `&p->buf[i]`, `p->buf + k`), matching gcc/clang; a struct's
  last array member counts as flexible. Array members that decay without
  `&` fold statically.
- Tests: thread-interleaving suite covering `_Thread_local`, `tss` and
  thread identity across `thrd_sleep`, mutex, condition-variable and join
  hand-offs.

## [0.19.1] - 2026-10-19

- Fixed: `<string.h>` declares size arguments and the `strlen`/`strspn`/
  `strcspn`/`strxfrm` returns as `size_t` (was `long`).
- Fixed: `-c=native` supplies the C23 `<math.h>` functions host libms lack
  (`exp10`, the pi-scaled trig family, `fmaximum`/`fminimum` family,
  `totalorder`, `canonicalize`, payload functions, `llogb`, `fromfp` family)
  and `memset_explicit`, `memalignment`, `free_sized` and
  `free_aligned_sized`, running the same code as the VM.
- Fixed: `-c=native` renames block-scope typedefs, tags and enumerators that
  reuse a name visible at the top of their function.
- Fixed: `-c=native` gives a called `gnu_inline` body a weak out-of-line
  definition, and a later non-inline definition replaces an earlier
  `gnu_inline` one.
- Fixed: a program's own definition of a library function (`fabs`) wins over
  the host builtin under `-c=native`.
- Fixed: user macros no longer rewrite `-c=native` output after the include
  replay (`#define int long long`).
- Fixed: `-c=native` finds the host headers when an `-I` names CCCC's include
  directory, no longer replays `#warning`, and passes `-w` to the host
  compiler.

## [0.19.0] - 2026-10-19

- Added: `-c=native` and `-m` enforce `--checked-pointers` (and
  `#pragma cccc config(checked_pointers)`) in the C they emit. A violation
  prints the VM's banner and exits with status 255. See `docs/SAFETY.md`.
- Changed: `-c=native` and `-m` no longer warn that `--checked-pointers` is
  ignored. `-c=generated` still drops it.

## [0.18.0] - 2026-10-19

- Added: `@ctor("prefix")` names the constructor `prefix_T_new`.
- Added: `@default(expr)` on a struct member gives `@ctor`'s `T_new` that value
  instead of a parameter. See `docs/DERIVE.md`.
- Changed: `@generate_getters`, `@generate_setters`, `@generate_accessors` and
  `@generate_constructor` are `@getters`, `@setters`, `@accessors` and `@ctor`;
  the constructor is `T_new` (was `T_create`); `@constructor_skip` is
  `@ctor_skip`. The old names warn as unknown attributes.
- Removed: `@builder`, `@builder_default` and `@builder_required`. `@ctor` with
  `@default` covers them.
- Fixed: a custom attribute named like a C keyword (`@default`) reached its
  handler as an unknown attribute.
- Added: `CopyNode` copies labels, `goto`, `switch`/`case`, local declarations,
  compound assignments, struct-returning calls and checked-pointer accesses.
  Each copy has its own labels and locals. `static` locals and checked-pointer
  declarations are still refused.
- Added: `CopyNode` copies `cleanup` variables and variable-length arrays; each
  copy runs its own handler and has its own size.
- Fixed: a `CopyNode` copy that only reads an enclosing variable-length array
  read an uninitialised size.

## [0.17.2] - 2026-10-18

- Added: `CopyNode(n)` deep-copies an expression or simple statement so a macro
  can link it into a second list.
- Fixed: `Block`, `BlockAddStmt`, `NodeList`, compound-literal and call
  arguments, `Quote` list splices and macro results that are statements reject
  a node another list already holds, instead of rewriting the earlier list.
  See `docs/COMPTIME_POLICY.md`.

## [0.17.1] - 2026-10-18

- Fixed: compiler builtins that link nodes (`Block`, `NodeList`, `BlockAddStmt`,
  `LoopSetBody`, `SwitchAddCase`, `SwitchSetDefault`, compound-literal and call
  arguments, `StructAddField`, `Quote` statement placeholders) reject a node
  that would contain itself or repeat in one list, instead of crashing or
  hanging the compiler. `Block` and call arguments end their `next` chain.
  See `docs/COMPTIME_POLICY.md`.

## [0.17.0] - 2026-10-18

- Changed: compiler builtins check their string, string-array, out-pointer and
  buffer arguments against VM memory, including when called through a function
  pointer. `Quote`/`QuoteLazy` check each `$N` argument is a `Node *` handle
  and that the template gets as many arguments as it references;
  `MacroErrorAt`/`MacroWarningAt` format with the comptime `printf` rules.
  `$N` inside string literals, character literals and comments is not a
  placeholder. See `docs/COMPTIME_POLICY.md`.
- Fixed: a file-scope macro's string arguments are VM memory, so `strlen(name)`
  and `name[0]` work on them.
- Changed: a comptime function that returns through an overwritten saved frame
  pointer or return address is a compile error instead of running on a forged
  frame. See `docs/COMPTIME_POLICY.md`.
- Fixed: `asprintf` no longer crashes; its format string was passed as a
  variadic argument.
- Changed: comptime libc calls check their pointer arguments against VM
  memory, and `free`, `realloc`, `longjmp` and the wide-integer, decimal and
  overflow operations are confined too; bad pointers are compile errors.
  Strings and structs libc returns from host memory (`strdup`, `strerror`,
  `localtime`, ...) are VM copies, so comptime code can read them. `%n` is
  rejected in comptime `printf`. See `docs/COMPTIME_POLICY.md`.
- Fixed: comptime code can read what granted libc calls return: `getenv`,
  `getpwnam`, `getgrnam`, `localeconv`, `gethostby*`, `getservby*`,
  `getprotoby*`, `getcwd`, `realpath` and the `scandir` list are VM copies, and
  `glob`, `readdir`, `fts_read`, `getaddrinfo`, `wordexp`, `hsearch`,
  `tsearch` and `mmap` results are readable until their release call.
- Changed: comptime libc calls follow the pointers stored in their arguments
  (`strtok_r` save pointer, `iconv` buffers, `mbsrtowcs`/`wcsrtombs` source,
  `insque`/`remque` links, `hsearch` key, `tsearch`/`tfind`/`tdelete`/`twalk`
  roots, `getopt`/`getopt_long` argv and long options, `alphasort` entries,
  `regex_t`) and reject NULL callbacks; a bad pointer is a compile error.
  `hsearch` keys are read-only to comptime code. See
  `docs/COMPTIME_POLICY.md`.
- Fixed: a host fault inside a native call during comptime prints one line and
  exits with status 1 instead of risking a hang on a libc lock. See
  `docs/COMPTIME_POLICY.md`.

## [0.16.0] - 2026-10-17

- Added: `--sandbox` confines the process with the OS sandbox (macOS
  `sandbox_init`, Linux landlock + seccomp): no network, writes only to output
  and temp directories, exec only of cccc and the host C toolchain. See
  `docs/SECURITY.md`.
- Changed: URL `#include` and `#embed` download only with `--allow-url`; cached
  copies still resolve.
- Changed: runtime `dlopen` returns NULL under `--disable-ffi`.
- Changed: `CcccExecutable` and `CcccObject` child cccc processes run with
  `--no-config` and inherit the build's `--comptime-allow`, `--allow-url`,
  `--disable-ffi` and `--ffi-allow`/`--ffi-deny`.
- Changed: comptime loads and stores outside VM memory are compile errors
  instead of host crashes. Compiler builtins work under `--disable-ffi` and
  return names and arrays as VM copies. See `docs/COMPTIME_POLICY.md`.

## [0.15.0] - 2026-10-17

- Added: `CcccObject` build target compiles a cccc source to an object file
  (`cccc --compile=object`) that other targets take with `LinkWith`. See
  `docs/BUILD_MODE.md`.

## [0.14.0] - 2026-10-17

- Added: `-c=object` compiles to an object file with the host compiler and
  does not link. See `docs/NATIVE.md`.
- Changed: repeating `-c` with a different format is an error.

## [0.13.0] - 2026-10-17

- Added: config files. A user file and the nearest project `.cccc.toml` set
  default command-line options in TOML; a project file that grants
  capabilities needs approval first. See `docs/CONFIG.md`.
- Added: `--comptime-allow=<class>[,...]` grants comptime code the `fs`,
  `exec`, `net`, `env`, `dlopen`, `thread`, `signal` and `ipc` capability
  classes, or `all`. See `docs/COMPTIME_POLICY.md`.
- Changed: comptime code can call only a safe subset of libc unless a class is
  granted; calls to anything else are denied.
- Changed: taking the address of `setjmp`, `_setjmp` or `sigsetjmp` is a
  compile error in the VM.
- Fixed: a static or global function pointer initialised with `free`
  (`static void (*p)(void *) = free;`, `{0, realloc, free}`) failed with
  `undefined relocation target: free` in the VM and under `-c=native`.
- Fixed: the address of `signal`, `raise`, `longjmp`, `_longjmp`,
  `siglongjmp`, `dlopen`, `dlsym`, `dlclose` or `dlerror` evaluated to NULL in
  the VM, or failed in a static initialiser; it is now callable through a
  function pointer.

## [0.12.0] - 2026-10-17

- Added: `@range(min, max)` on an integer, enum or bit-field member, with
  `@validate` and `@clamp` generating `T_validate` and `T_clamp` for a struct.
  See `docs/DERIVE.md`.
- Added: `@compare_order(N)` and `@compare_desc` choose the order and direction
  `@compare` uses for a member.
- Added: `@equals`, `@hash`, `@dump`, `@compare`, `@to_json` and `@from_json`
  treat `@owned` members by content.
- Added: `@clone` and `@destroy` work through a pointer to a struct that is
  declared but not yet defined.
- Added: `@bitflags` accepts composite aliases such as `RW = R|W`, and member
  attributes can follow a bit-field width.
- Added: `ConstIntValue(node, &out)` reads an integer constant expression
  argument of a macro or attribute. See `docs/MACROS.md`.
- Added: `@serialize_skip`, `@accessor_skip` and `@constructor_skip` leave a
  member out of `@serialize` / `@deserialize`, the getters and setters, and
  `@generate_constructor`.
- Changed: an `@name` attribute with no registered handler warns by default
  (`-Wunknown-attributes`).
- Fixed: `-c=native` output for array-of-array subscripts, widening casts and
  empty structs.
- Added: `@builder` generates a `T_builder` type with chaining setters and
  `T_build` for a struct, with `@builder_default` and `@builder_required`
  members. See `docs/DERIVE.md`.
- Added: `@clone` and `@destroy` generate `T_clone` and `T_destroy` for a struct,
  deep-copying and freeing the pointer members marked `@owned`. They need
  `#include <stdlib.h>`. See `docs/DERIVE.md`.
- Added: `@compare` generates `int T_compare(const T *a, const T *b)` for a
  struct, ordering by members in declaration order. See `docs/DERIVE.md`.
- Added: `@bitflags` on an enum of power-of-two flags generates `T_set`,
  `T_clear`, `T_toggle`, `T_has` and `T_format`. See `docs/DERIVE.md`.
- Added: `@equals`, `@hash` and `@dump` generate `T_equals`, `T_hash` and
  `T_dump` for a struct, with `@equals_skip` and `@dump_skip` to leave members
  out. `@dump` needs `#include <stdio.h>`. See `docs/DERIVE.md`.
- Added: `@to_json` and `@from_json` generate `T_to_json` and `T_from_json` for
  a struct; they need `#include <cccc/json.h>` as well as `<cccc/derive.h>`.
  See `docs/DERIVE.md`.
- Changed: the built-in attributes (`@serialize`, `@deserialize`,
  `@enum_to_string`, `@enum_from_string`, `@generate_*`) need
  `#include <cccc/derive.h>`; without it they are a compile error that names
  the header.
- Fixed: a comptime-generated function can call a `static` function of the
  program.
- Fixed: `MakeCast` on a `MakeMember` or `MakeSubscript` node crashed the
  compiler.

## [0.11.1] - 2026-10-16

- Fixed: `switch (f(x))` with a function call as the controlling expression
  always took the first matching branch in the VM.
- Fixed: `Serialize`, `Deserialize`, `@serialize` and `@deserialize` need no
  particular alignment of the buffer; `Deserialize` rebuilds the value member
  by member instead of casting the buffer.

## [0.11.0] - 2026-10-16

- Fixed: a guest thread that runs between two `--testing` tests no longer leaves
  its stack in the VM, so the next test no longer runs on freed memory (a
  sporadic crash on macOS). Blocking calls such as `usleep` and `wait` now keep
  the calling thread's thread-local storage across the wait.
- Fixed: a guest signal handler that runs right after a call returns no longer
  replaces that call's result with the signal number.
- Fixed: a `[[cccc::comptime]]` macro call is expanded in `case` labels
  (including ranges), enumerator values, `_Static_assert` conditions and
  file-scope array lengths. See `docs/MACROS.md`.
- Fixed: `@serialize`, `@deserialize`, `@enum_to_string`, `@enum_from_string`,
  `@generate_getters`, `@generate_setters` and `@generate_constructor` accept
  `typedef` declarations as well as tagged ones.
- Fixed: `@serialize` copies bit-fields and the fields of anonymous
  struct/union members.
- Fixed: `@generate_getters`, `@generate_setters` and `@generate_constructor`
  handle array members and anonymous struct/union members.
- Fixed: `@enum_to_string` accepts enums with aliased values; the first name
  wins.
- Fixed: `GenerateSum`, `GenerateMap`, `GenerateReduce` and `GenerateFilter` can
  be called more than once for the same element type.
- Added: `@generate_getters("Prefix")` and `@generate_setters("Prefix")` name
  the functions `Prefix_get_<field>` / `Prefix_set_<field>`, and
  `@generate_accessors` publishes both.
- Added: `StringLiteralValue(node)` reads a string literal macro argument.

## [0.10.8] - 2026-10-16

- Fixed: a thread left running by a passing `--testing` test no longer unbalances
  VLA/`alloca` reclamation in later tests. Reclamation stays off for the rest of
  a run once a thread has been created.
- Fixed: a thread created before the program's first `pthread_self`, mutex or
  TSS call is tracked, so it is reaped at exit.
- Fixed: `--testing` stops the threads a failed or timed-out test left behind,
  and a failed `Assert` in a setup hook, forked `exit_code=` child, worker or
  callback no longer crashes, hangs or re-runs later tests. See
  `docs/TEST_MODE.md`.

## [0.10.7] - 2026-10-15

- Fixed: `--testing` no longer hangs after a test fails an `Assert` or times out.
  A later `exit_code=` test or a test that starts a thread now runs normally.

## [0.10.6] - 2026-10-15

- Fixed: a VLA length expression with a side effect runs once under
  `-c=native`/`-m`, as in the VM. A block-scope typedef of a variably modified
  type evaluates its length where it is declared, in the VM too.
- Fixed: `--testing=native` honours `--test`, `--test-suite`, `--fail-fast`,
  `--test-timeout` and `--list-tests`, and the built binary accepts them
  directly. `--test-format=plain|json` is refused with it. See
  `docs/TEST_MODE.md`.

## [0.10.5] - 2026-10-15

- Fixed: `__builtin_fabs`/`sqrt`/`floor`/`ceil`/`trunc`/`round`/`copysign`/`fmin`/`fmax`/`fma`
  (+ `f`/`l`) with constant arguments fold at compile time, so they are valid in
  static initializers, `constexpr` and `_Static_assert`. See `docs/STDLIB.md`.

## [0.10.4] - 2026-10-15

- Changed: a same-scope typedef redefinition with a different type, or a typedef
  of a name already bound as an enumerator or variable, is an error. A clash
  between two system headers warns. See `docs/TYPES.md`.
- Fixed: `-c=native` and `-m` define a tagless struct or union once under a
  `__cccc_anon<N>` tag, so assigning one anonymous-struct lvalue to another
  builds with the host compiler. See `docs/NATIVE.md`.
- Fixed: `-c=native` defines a `static` whose struct type is declared in the
  function, and a tagless global with no initializer, instead of dropping it.
- Fixed: `tests/host/test_watch_cli.c` builds on Linux.

## [0.10.3] - 2026-10-14

- Added: `--link-arg=ARG` passes `ARG` to the host link line under `-c=native`;
  `AddLdFlag` and `AddFramework` on `CcccExecutable` use it. See
  `docs/NATIVE.md` and `docs/BUILD_MODE.md`.
- Added: the `__sync_*` atomic builtins and the `__sincos*_stret` FFI functions.
- Added: unchecked `format` archetypes (`CFString`, `NSString`, `os_trace`, ...)
  are accepted; an unknown archetype warns.
- Fixed: GNU `extern inline` bodies are inline-only definitions.
- Fixed: `__inline`, `__restrict` and `__thread` spellings, and gnu89 `inline`,
  are accepted before C99/C11.
- Fixed: an SDK `Availability.h` no longer taints `<stdio.h>`; only a file
  identical to cccc's own header is treated as its copy.
- Fixed: `-c=native` declares input functions, types and transitive anchors
  ahead of a replayed implementation-style header, and copies multi-line
  directive comments whole.
- Fixed: `-Wparentheses` parenthesizes operand pairs.
- Fixed: a header that ends in a conditional after its include guard's `#endif`
  (the stb two-include layout) is no longer skipped on a second `#include`.

## [0.10.2] - 2026-10-14

- Added: `__builtin_fabs`, `sqrt`, `floor`, `ceil`, `trunc`, `round`,
  `copysign`, `fmin`, `fmax` and `fma` (and their `f`/`l` forms) without
  `<math.h>`. See `docs/STDLIB.md`.
- Added: `_Float16` as a storage-only type: declarations, layout, pointers and
  prototypes work; using a value is an error. See `docs/COVERAGE.md`.
- Changed: `#include_next` and `__has_include_next` continue into framework
  directories.
- Fixed: attributes that SDK headers use (`availability`, `visibility`, ...)
  no longer run the comptime pass on programs without comptime code.
- Added: framework header lookup. `-F`/`--framework-path` and `--iframework`
  resolve `<Name/X.h>` to `Name.framework/Headers/X.h`, and `--sysroot` adds
  `<sdk>/System/Library/Frameworks`. See `docs/HEADERS.md`.
- Changed: `-F` is `--framework-path`; `--disable-ffi` has no short form.
- Fixed: a byte that is not valid UTF-8 inside a comment is no longer a
  compile error.

## [0.10.1] - 2026-10-14

- Added: `--repl --watch` reloads the files `:load` read when they are saved,
  at the prompt without pressing Enter. See `docs/WATCH.md`.
- Changed: `--watch` reloads only the files that include a changed header
  instead of every file of the program, and watches a header a reload starts
  to include.
- Fixed: a long `--watch` session no longer grows the list of input files by a
  duplicate on every reload.
- Release tarballs include `THIRD_PARTY_NOTICES.md` (chibicc, stb_sprintf,
  dmon, libbacktrace).

## [0.10.0] - 2026-10-14

- Added: `--watch` hot-reloads a running program when a source file is saved
  (changed functions are redefined, globals keep their values), restarts it
  when it exits, and waits for the fix after a compile error. With `--build`,
  or when the file carries `[[cccc::build]]`, a save rebuilds. See
  `docs/WATCH.md`.
- Added: `--watch-path`, `--watch-ignore`, `--watch-debounce`, `--watch-clear`
  and `--watch-restart`.
- Added: `Watch`, `WatchGlob`, `IgnoreWatch` and `BuildWatching` in
  `building.h`.
- File watching uses the vendored dmon library (`src/dmon.h`); macOS builds link
  CoreServices.

## [0.9.2] - 2026-10-14

- Fixed: a debugger condition, return-value expression or REPL `:type`
  expression with tokens after the expression (`x == 1 garbage`) is an error
  instead of being read as the part before them.
- Fixed: `arr[i]++`, `arr[i] += k` and the same on a multi-dimensional or
  struct-member local array no longer tag the array under `-3`.
- The `test` build target also runs the host tests (`tests/host/test_*.c`).

## [0.9.1] - 2026-10-14

- Fixed: a debugger condition that names a `__block` local, a variable-length
  array or a struct or union parameter reads its value (a `__block` condition
  never fired).
- Fixed: a debugger condition with a compile error (a non-scalar result, or a
  block that captures an array or `__block` local) prints the error and counts
  as false instead of ending the process.
- Fixed: a debugger condition reads paused-frame locals after a string literal
  and inside statement expressions.
- Fixed: a block literal in a debugger condition captures paused-frame locals
  by value.
- Fixed: `++`, `--` and compound assignment on a local no longer record its
  address under `--dangling-pointers`, which removes a hashmap write per
  update and a stale tag a later frame could hit.
- Fixed: re-taking an address under `-3` no longer leaks its provenance entry.
- Fixed: a block-scope `_Thread_local` without `static` or `extern` is an error,
  as in gcc and clang.
- Fixed: a block-scope `static _Thread_local` variable is private to each
  thread instead of shared.

## [0.9.0] - 2026-10-14

- Fixed: the debugger's `reload` and `fix` print their compile errors (they
  were silent under `-g`).
- Fixed: a `reload` of a file with `[[cccc::comptime]]` functions leaves the
  session's comptime functions in use. It refuses a changed or new comptime
  function or variable, a file-scope comptime call, and a new or changed
  function that calls a comptime function; `fix` refuses such a call with a
  clear message.
- Changed: a debugger condition or REPL expression that only reads program
  state gives back its string literals, block literals and labels too, and
  data freed below newer data is reused by later conditions.
- Fixed: `--dangling-pointers` no longer reports a variable-length array's
  pointer slot as dangling when an earlier call left a stale tag at that
  address.
- Fixed: `[[cleanup]]` written before the type in a `for` initializer
  (`for (__attribute__((cleanup(f))) int v = 0; ...)`) now runs its handler
  when the loop ends.
- Fixed: a `for` initializer accepts any declaration: C23 `auto`, `constexpr`,
  `_Alignas`, `static`, `extern` and `typedef`. Before C23, the last three
  warn under `-Wpedantic`; `_Thread_local` without `static`/`extern` is an
  error. A `const` or `constexpr` initializer is no longer rejected as an
  assignment.
- Added: debugger `return` and `restart` run the `[[cleanup]]` handlers the
  popped frames still owe; `return -n` and `restart -n` skip them.
  `cc_debug_return` and `cc_debug_restart` take a `run_cleanups` argument.
- Fixed: the address a `[[cleanup]]` handler receives is no longer tagged for
  dangling-pointer detection (`-3`), which could report a stale frame epoch.
- Added: the debugger's `reload <file>` and the REPL's `:reload <file>` re-read
  an edited source file and redefine the functions whose bodies changed;
  globals and statics keep their values. Embedders use `cc_debug_reload` and
  `cc_repl_load_file`.
- Fixed: a conditional breakpoint (and `print`, `return <expr>`) no longer
  trips dangling-pointer detection (`-3`) when a returned frame left a
  `&local` tag on the slot being read.
- Added: the REPL redefines functions (hot reload). Direct calls, function
  pointers and host callbacks reach the new body; block-scope statics keep
  their storage. Embedders use `cc_repl_init`, `cc_redefine_function` and
  `cc_repl_eval`.
- Added: debugger frame control. `bt`, `frame`, `up` and `down` select a call
  frame (`print` reads its locals); `return [expr]` pops it as if it returned
  `expr`; `restart` re-enters it from the start with its original arguments
  (explicit `-g`). Embedders use `cc_debug_return` and `cc_debug_restart`.
- Added: debugger `fix <function>` replaces a function while paused (edit and
  continue with `restart`). Embedders use `cc_debug_fix`.
- Fixed: long REPL and debugger sessions no longer grow the VM's text and data
  segments: expression and `return <expr>` wrappers are given back after use,
  and conditional breakpoints with the same condition in one function share
  one compiled wrapper that is released when its breakpoints are deleted.
  Code freed below newer code is reused by later conditions.
- Fixed: the debugger's symbol table grows on demand; `print` and watchpoints
  find globals and locals past the first 4096 symbols.
- Added: `fix <function>` opens `$VISUAL`/`$EDITOR` on the function's current
  source when a terminal is attached, and offers to re-edit on a compile error.
  Macros of the last file expand in `fix` source.
- Fixed: `break file:line` finds code compiled at the REPL or by `fix`;
  breakpoints follow a redefined function.
- Fixed: a conditional breakpoint evaluating to a negative number counted as
  an evaluation error; a parse error in a breakpoint condition no longer
  unwinds into the program's error handler; a REPL expression unit no longer
  renumbers the stack offsets of the function defined before it.
- Fixed: a REPL function defined after an earlier prototype line is compiled;
  every REPL definition after the first is no longer parsed as a nested
  function; a `static inline` function defined at the REPL is compiled.

## [0.8.5] - 2026-10-12

- Added: `--asm-passthru` accepts bit-field output operands (`=r`, `+r`); a
  bit-field with a memory constraint is a compile error.
- Changed: tests run through `build.c` targets. `test` runs the `[[cccc::test]]`
  suites in VM and `-c=native` mode; `test_warnings`, `test_serialize`,
  `test_build_mode`, `test_safety`, `test_parallel`, `test_macros` and
  `test_misc` (all: `test_extra`) run the single-file tests. The Python
  runner (`tools/run_tests.py`, `tools/tests.py`, `tools/testing/`) is gone.

## [0.8.4] - 2026-10-11

- Added: GNU extended `asm` parses with operands, clobbers and `goto` labels
  and is emitted verbatim under `-c=native`/`-m`. The VM leaves outputs
  untouched; `--asm-passthru` passes the operands to one batched host thunk
  build.
- Fixed: rvalue expressions (arithmetic, shifts, `?:`, comma, casts, calls,
  assignment and `++`/`--` results, statement expressions) drop
  `const`/`volatile`/`restrict`/`_Atomic`. Assigning to or taking the address
  of a comma, call or cast result is a compile error.
- Fixed: `-c=native` assigns through a compound literal or one of its members
  (`(int){3} = 4`, `(struct P){1,2}.x = 5`).
- Fixed: `-c=native` emits no `-Wunused-value` noise for a discarded
  `_Atomic` `++`/`--`/`op=` or a discarded comma operand.
- Fixed: postfix `++`/`--` on an `_Atomic` operand yields the CAS loop's
  old value, and `int a = ++ai;` on an `_Atomic int` builds under Apple clang.
- Fixed: an initialised flexible-array-member global keeps its declared
  struct type in expressions (`sizeof`, `typeof`, `&g + 1`, struct copy); a
  non-empty flexible initializer on an automatic object is a compile error.
- Fixed: a `static _Thread_local` global is declared once under gcc.
- Fixed: a diagnostic caret mirrors tabs on tab-indented source lines.
- Changed: libbacktrace is a git submodule (`vendor/libbacktrace`); an
  uninitialised submodule builds without it.

## [0.8.3] - 2026-10-10

- Fixed: casting an unsigned (or `_Bool`/pointer) scalar to a `_BitInt` wider
  than 64 bits zero-extends in the VM; it used to sign-extend, so
  `(_BitInt(256))0xffffffffffffffffull` came out as -1.
- Fixed: unary `+`, `~`, `<<` and `>>` promote a `char`/`short`/`_Bool`/enum
  operand to `int`, so `sizeof(~c)` is 4 as in gcc and clang.
- Added: `-c=native`/`-m` lower anonymous struct/union members, flexible array
  members and `&(struct S){...}.member` in a struct or union with a
  `_BitInt(N>128)` bit-field, and a global initializer keeps the pointers
  inside such a struct. A pointer in a union or at a `packed` offset is still
  refused.

## [0.8.2] - 2026-10-10

- Added: `-c=native`/`-m` lower a struct or union with a bit-field whose
  declared type is a `_BitInt` wider than 128 bits, by emitting it as opaque
  bytes in CCCC's own layout. Anonymous and flexible array members in such a
  struct, and pointers in its global initializer, are still refused.
- Fixed: `_Alignof(expr)` and `__alignof__(expr)` report a typedef's
  `aligned(N)` when the expression's type is that typedef (`*p`, `p[1]`,
  `-g`, `g + g`), and the plain type's alignment when the result is another
  type (`g + 1`). Under `--compiler-family=clang` a cast keeps the typedef's
  alignment too.
- Added: `-mavx`, `-mavx2` and `-mavx512f` on x86_64. They define `__AVX__`,
  `__AVX2__` and `__AVX512F__`, raise gcc's cap on C11 `_Alignof(type)` of a
  wide vector to 32 or 64, and are forwarded to `-c=native`'s compiler.
- Fixed: `_Alignof(type)` and `__alignof__(type)` of a typedef with
  `aligned(N)` report `N` instead of the underlying type's alignment.

- Fixed: on Linux x86_64 under `--compiler-family=gcc`, C11 `_Alignof(type)`
  and `_Alignas(type)` of a 32- or 64-byte vector (or a struct, union or array
  holding one) report gcc's 16 instead of the vector's size. `__alignof__`,
  `_Alignof expr` and `-c=native` constants are unchanged.
- Added: `__alignof__` and `__alignof` are keywords available before C11.

## [0.8.1] - 2026-10-06

- Added: under `-2` and `-3`, a subscript directly over a fixed-size local or
  global array (`buf[k]`, `m[i][j]`, `s.arr[k]`) is bounds-checked at the
  access. `a[size]` and negative indices are reported; `&a[size]` and
  `a + size` stay legal. A struct's trailing array is treated as flexible.
- Fixed: `-x cl -m`/`-c=native` run outside the repo (no `-I`) no longer drops
  the OpenCL typedefs (`float4`, `uint`, ...) from the output.
- Fixed: `-c=native` no longer fails the layout assertion for a struct holding a
  32- or 64-byte vector under x86_64 gcc.
- Fixed: `p++`, `p--` and `p[-1]` on a pointer into stack or global memory no
  longer report "Negative array index" under `-2` and `-3`.
- Fixed: `--dangling-pointers` no longer reports a live frame's own array
  access (`buf[i]`, `m[i][j]`, `s.arr[i]`, including `+=` and `++`) as
  dangling when a returned frame's local once occupied that address.
- Fixed: locals and parameters with alignment above 8 (`_Alignas(N)`,
  `__int128`, wide `_BitInt`, wide vectors) have correctly aligned addresses
  in the VM, and `--dangling-pointers` tracks the aligned address.
- Fixed: `vector_size` types are aligned as the host ABI aligns them (capped
  at 16 on aarch64 and macOS), so locals, struct layout and `-c=native`
  agree.

## [0.8.0] - 2026-10-05

- Added: GNU `__auto_type` declares a variable typed from its initializer.
- Added: `-Wauto-declarator`, part of `-Wall`, flags an `auto` or `__auto_type`
  declaration that gcc rejects (a pointer declarator such as `auto *p = &x;`,
  or several declarators) under `--compiler-family=gcc`, the default. cccc
  accepts the declarations.
- Fixed: a declarator attribute after a C23 `auto` name
  (`auto t __attribute__((unused)) = a;`) is accepted.
- Fixed: a `cleanup` attribute on a variable-length array runs at scope exit
  and receives a pointer to the array, in the VM and under `-c=native`. A
  pointer-to-VLA local with both a `cleanup` and an initializer compiles under
  `-c=native` instead of failing with "redefinition".
- Fixed: the `cleanup` function of a `__block` variable receives the
  variable's own address in the VM.
- Fixed: a VLA declared in a `for` initializer (`for (int n = 3, v[n], i = 0;
  ...)`) compiles under `-c=native`, `-m` and `-c=generated` instead of
  failing with "cannot be serialized to C".
- Fixed: `-Wattributes` warns that `cleanup` does not apply to types in a cast,
  `sizeof` or `__typeof__` type-name, as gcc does.
- Fixed: `__alignof__(v)` and `_Alignof(v)` on a variable or struct member
  report its declared `aligned(N)` / `_Alignas(N)` alignment, not just its
  type's.
- Fixed: `-Wtautological-compare` no longer reports a self-comparison for
  operands that fold to a constant, such as `sizeof(struct P) == 8`.

- Fixed: subscripting a vector rvalue (`(a + b)[0]`, `f()[3]`) is accepted
  instead of failing with "not an lvalue". Assigning to such a lane is still
  an error.

- Fixed: `vector_size` after a pointer, array or function declarator
  (`int *q VS;`, `int b[2] VS;`, `int f(void) VS;`) vectorizes the innermost
  scalar, as in gcc. It was rejected as not a scalar type.

- Fixed: `vector_size` written before the type or after the type specifier
  (`[[gnu::vector_size(16)]] int v;`, `int __attribute__((vector_size(16))) v;`)
  now makes a vector, as in gcc. It was a parse error in declarations, and
  silently ignored in cast, compound-literal and `sizeof` type-names.

- Fixed: compile time no longer grows quadratically with the number of
  globals. A 54,000-line file with 6,000 functions compiles in 0.5 s instead
  of 5.5 s.
- Fixed: computing token columns no longer costs about 0.2 s on every compile
  that uses comptime.
- Fixed: GNU attributes that `__has_attribute` reports as recognized, such as
  `cold`, `hot`, `noinline` and `always_inline`, are accepted silently instead
  of warning "unknown attribute" under `-Wattributes`.
- Fixed: an unknown attribute after a function declarator warns once instead
  of three times.
- Fixed: an attribute after a parenthesised declarator, as in
  `int (*fp)(void) __attribute__((aligned(16)))`, is accepted instead of
  failing with "expected ','". Function attributes such as `format` and
  `nonnull` on a function pointer apply to calls through it.
- Fixed: `[[__gnu__::name]]` is accepted as `[[gnu::name]]`, reserved
  `__name__` spellings work inside `[[...]]`, and `__has_c_attribute(gnu::x)`
  reports recognized GNU attributes.
- Fixed: `-c=native` runs `cleanup` attributes instead of dropping them.
- Fixed: a `break`, `continue`, `return` or `goto` that leaves a scope before a
  cleanup variable's declaration no longer runs that variable's cleanup.
- Fixed: cleanup variables declared in a `for` header or inside a statement
  expression `({ ... })` are cleaned up.
- Fixed: a `cleanup` attribute applies only to the automatic variable that
  names it. A declaration typed from that variable (`__typeof__(v) t`) no
  longer inherits it.
- Fixed: a `cleanup` attribute at the start of a declaration
  (`__attribute__((cleanup(f))) int a, b;`, `[[gnu::cleanup(f)]] int a;`,
  `int __attribute__((cleanup(f))) *p;`) applies to every declarator instead
  of being dropped, and `int a, __attribute__((cleanup(f))) *p;` applies it to
  `p`.
- Changed: as in GCC, `cleanup` is ignored on a typedef, struct member,
  parameter, function, global or `static` local, with a `-Wattributes`
  warning. Static locals and variables of such a typedef no longer run it. A
  C23 `[[gnu::cleanup(f)]]` after the type specifier is ignored.
- Added: `--vm-profile` reports comptime execution in its own section, and
  works with `-c=native`, `-c=generated` and `-m`. With `--json`, the comptime
  profile is a nested `"comptime"` object.
- Fixed: `--vm-profile` no longer drops `main`'s counts when the program
  registers an `atexit` handler. The report no longer has a `cycles` line or
  JSON key; it counted only the last VM entry, so use `total_opcodes`.

## [0.7.3] - 2026-10-02

- Fixed: a `#pragma omp` inside a function body in an emit block no longer
  fails to compile. It now builds an OpenMP region, as it would in ordinary
  source, and is still kept in the emitted output.
- Fixed: `-Wunused` no longer reports a function label as unused when its only
  `goto` is spliced in from a `Quote()`/`QuoteLazy()` template.

## [0.7.2] - 2026-10-01

- Fixed: `-c=generated` no longer copies `#pragma omp` lines, `#include <omp.h>`
  or its guard into the output. A region in macro-generated code runs on the
  thread pool, and the runtime is emitted only when generated code needs it.
  `-m` and `-c=native` no longer replay `#pragma omp` at file scope.
- Added: `_Pragma("omp ...")` in a `Quote()` template builds an OpenMP region.
- Fixed: `-Wlogical-op` and `-Wtautological-compare` no longer fire inside
  `#if`/`#elif` expressions, such as those in the bundled `<stddef.h>`.

## [0.7.1] - 2026-10-01

- Fixed: a call to a single-return `static inline` function evaluated its
  arguments more than once, could report a spurious "not an lvalue", and could
  clobber the caller's locals. Such calls are now ordinary calls.

## [0.7.0] - 2026-10-01

- Added: OpenCL C 1.2 kernels. A `.cl` file, or `-x cl`, is read as OpenCL C:
  `__kernel`/`__global`/`__local`/`__constant`/`__private`, the `get_*_id` and
  size functions, `barrier`, `mem_fence`, the 1.2 atomics (including
  `atomic_xchg` on `float`), vector types of 16, 32 and 64 bytes with
  swizzles and vector literals, and `-cl-std=CL1.2`. See `man/OPENCL.md`.
- Added: `<cccc/kernel.h>` and `<cccc/opencl.h>` are embedded in the compiler,
  so they resolve from any directory without `-I`.
- Fixed: `auto`, `typeof_unqual`, `__atomic_fetch_*` and `_Atomic` compound
  assignment on a `[[cccc::global]]`, `[[cccc::local]]` or
  `[[cccc::constant]]` object gave the compiler's temporary that address space,
  and kernel code rejected it as "a global-memory object on the stack".
- Limitation: no vectors under 16 bytes (`float2`, `int2`, ...), no 128-byte
  vectors, no OpenCL built-in function library, no `half`.

## [0.6.0] - 2026-10-01

- Added: `-c=native` and `-m` run `cccc_launch`, the work-item builtins,
  barriers and `[[cccc::local]]` memory on CPU threads. Kernels without a
  barrier run their work-groups in parallel on the thread pool; kernels with a
  barrier run a thread per work-item. See `man/KERNELS.md`.
- Fixed: `-m` emitted a call to an undefined `__cccc_launch` for a kernel
  launch.
- Limitation: `--test-run` and `-c=generated` do not support `cccc_launch`; a
  native barrier kernel starts a thread per work-item for every group.

## [0.5.0] - 2026-10-01

- Added: `-fopenmp` parses `#pragma omp` (`parallel`, `for`, `parallel for`,
  `critical`, `atomic`, `barrier`, `single`, `master`, with `private`,
  `firstprivate`, `shared` and `reduction`) and the VM runs each region on one
  thread. `<omp.h>` provides the runtime functions. See `man/OPENMP.md`.
- Added: `lastprivate` on `for` and `parallel for`, and `copyprivate` on
  `single`.
- Added: `-c=native` and `-m` run OpenMP regions on real threads from a thread
  pool emitted into the output; `num_threads`, `if`, `critical`, `atomic`,
  `barrier`, `single`, `reduction` and the `omp_*` runtime work as in OpenMP.
- Fixed: the compare-and-swap loop behind `__atomic_fetch_<op>` started with a
  plain read that raced with other threads' updates.
- Limitation: native OpenMP has no `ordered`, `dynamic`/`guided` schedules,
  pointer loop variables, `atomic capture` blocks or Windows support;
  `copyin`, `linear` and `threadprivate` are not supported.

## [0.4.1] - 2026-10-01

- Fixed: the macOS release job failed on flaky tests. The SIGCHLD siginfo and
  aio submission tests in `test_suite_posix.c` are skipped on macOS.

## [0.4.0] - 2026-10-01

- Added: `<cccc/kernel.h>` runs `[[cccc::kernel]]` functions over a grid of
  work-items in the VM. `cccc_launch(kernel, CCCC_RANGE(...), CCCC_RANGE(...),
  args...)` takes a kernel function or function pointer, and checks the
  arguments like a call. See `man/KERNELS.md`.
- Added: work-item builtins (`cccc_global_id`, `cccc_local_id`,
  `cccc_group_id`, the size queries, `cccc_work_dim`) and `cccc_barrier`.
- Added: `[[cccc::local]]` objects in a kernel body and `CCCC_LOCAL(bytes)`
  parameters live in per-work-group memory, under the VM's bounds checks.
- Added: `--kernel-max-group-size=N` (default 256).
- Fixed: a thread that faulted was not reported by the thread runner.
- Limitation: `-c=native` rejects the launch and builtins; kernel arguments
  must be integers, pointers or floats (up to 8 of each kind).

## [0.3.2] - 2026-10-01

- Fixed: `--sysroot` and `--use-system-headers` failed with "expected ','" on
  glibc 2.43 and newer.
- Fixed: `BuildRoot()` returned a dangling pointer, so `DirExists` and
  `FileExists` on it failed intermittently.

## [0.3.1] - 2026-09-30

- Fixed: `defined(__has_c_attribute)`, `#ifdef __has_builtin` and the same
  test for every other `__has_*` operator were false. The operators are now
  visible to `defined`, `#ifdef` and `#ifndef`, and evaluate outside `#if`
  as well (`int n = __has_builtin(__builtin_expect);`).
- Fixed: `#include_next` failed with "cannot open file" after the including
  header had itself included another header. `#include_next` and
  `__has_include_next` now search from the directory after the one the
  current file was found in; `__has_include_next` no longer always returns
  `0`.
- Docs: the `[[cccc::kernel]]` guard in `man/ATTRIBUTES.md` uses a nested
  `#if defined(__has_c_attribute)`, which is valid in C++ and Metal too.

## [0.3.0] - 2026-09-30

- Checked arrays: `int a _Checked[10]` / `char s _Nt_checked[11]` make the
  declared extent part of the array's checked type, so indexing is
  bounds-checked against it and decay to an unchecked pointer carries the
  bounds along. Multidimensional and variable-length checked arrays are
  compile errors. See `man/SAFETY.md`'s "Checked Arrays" section.
- Bounds-safe interfaces: a call passing a checked pointer to a parameter
  with declared bounds is verified at the call site (`CHKAB`) that the
  argument's own bounds imply the parameter's. An annotated parameter needs
  no extra attribute and places no restriction on an unchecked caller.
- Fixed: the VM loaded a clobbered register when an operand of
  `atomic_store*`, `atomic_exchange`, `atomic_compare_exchange_*` or
  `ckd_add/sub/mul` contained a call (a store stored the byte offset; the
  exchange and compare-exchange forms crashed).
- Fixed: an assignment stored a wrong value when its lvalue's address
  expression contained a block call, a wide `_BitInt` operation or a
  statement-expression.
- Fixed: `-c=native` emitted an invalid comparison between an `_Atomic`
  scalar load and a constant (`(_Atomic unsigned)7`).


- Checked-pointer bounds casts: `[[cccc::assume]]`/`[[cccc::dynamic]]`, the
  entry point for converting an unchecked pointer into a checked one.
  Attach in a cast's type-name alongside the claimed checked kind/bounds
  form (cast-only — a compile error on a declarator). `assume` takes the
  claim on trust with no runtime check; `dynamic` verifies it against the
  source's own declared-checked bounds via the existing `CHKAB` opcode,
  with a compile error when the source has no verifiable bounds. Both
  desugar to a compiler-generated declared-checked local, so every existing
  checked-pointer pass (`CHKR`, propagation, `CHKAB`) sees an ordinary
  checked variable with no new resolution machinery. The sanctioned
  unchecked→checked conversion inside a checked region, exempt from its
  cast ban. Also new: `dynamic_check(cond)` /
  `__builtin_cccc_dynamic_check(cond)` (with `_Dynamic_check(cond)` as a
  Checked-C-compat alias, recognized only when no symbol of that name is in
  scope) — a programmer bounds assertion that traps via the new `CHKDC`
  opcode when `cond` is false, under `--checked-pointers`; a no-op
  otherwise. See `man/SAFETY.md`'s "Checked Pointers" § "Bounds casts" and
  § "`dynamic_check`".
- Checked regions: `[[cccc::checked]]`/`[[cccc::unchecked]]` on a function
  definition or compound statement, `#pragma cccc checked/unchecked
  begin/end`, and the Checked C keyword spellings `_Checked { ... }` /
  `_Unchecked { ... }` (recognized positionally, not as reserved keywords, so
  an existing identifier of either spelling is unaffected), require every
  pointer declared within their extent to be a checked kind
  (`single`/`array`/`ntarray`) and forbid casting to or between unchecked
  pointer types there — the incremental-migration half of the
  checked-pointer layer. Always-on compile-time diagnostics, independent of
  `--checked-pointers`; see `man/SAFETY.md`'s "Checked Regions" section.

## [0.2.0] - 2026-09-11

- Inline build recipes and tests alongside `main()`: a single `.c` file can
  now carry `main()`, `[[cccc::test]]` functions, and a
  `[[cccc::build]]`/`[[cccc::build_target]]` recipe together, each invoked
  by its own flag (`cccc`, `cccc --testing`, `cccc --build`). `--build` no
  longer rejects a script for defining `main()`; a file with
  `[[cccc::build_target]]` factories and no entry now runs all of them
  automatically under `--build`.
- New `--no-emit-tests` flag: drops `[[cccc::test]]` bodies from
  `-c=native`/`-c=generated` output entirely, instead of emitting them
  inert.
- `RunCustom` build-script shell: builtins now honour redirects and pipes,
  the last same-direction redirect wins, pipe file descriptors are
  `CLOEXEC`, multi-redirect and `$(...)` command substitution work, and a
  pipeline no longer leaks write-end file descriptors or deadlocks.
- `#if`/`#ifdef` `defined()`/`__has_*` operators are now recognized when
  revealed by macro expansion, not only in the raw source text; an
  unrecognized `__has_*` operator now gets its own diagnostic.
- `--sysroot` now resolves the correct multiarch include directory, and
  `asm-label`/attribute ordering under `--sysroot` is fixed.
- `sys/statvfs.h` gets a real-header hand-off (closing out the
  self-hosting type-shadowing audit).
- A URL `#include` now mirrors nested project-header includes it reaches,
  and its default cache directory is resolved before `--url-cache-clear`
  checks for one.

## [0.1.0] - 2026-09-06

- Initial release.
