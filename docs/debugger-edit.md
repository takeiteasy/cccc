# Edit and Continue

Replace one function (`fix`) or re-read a whole file (`reload`) while the program is paused.

## `fix <function>`

Type the new definition on the following lines; the debugger takes it once the braces balance. New calls, function pointers taken earlier and host callbacks run the new body. A frame already running the old body finishes in it, and `restart` re-enters the function from the start with the new body.

```
(cccc-dbg) break leaf
(cccc-dbg) continue
Breakpoint hit at PC 98
(cccc-dbg) fix leaf
  ... int leaf(int x) {
  ...   return x * 3 + g;
  ... }
leaf replaced; new calls run the new body
(cccc-dbg) restart
(cccc-dbg) continue
```

With `$VISUAL` or `$EDITOR` set and a terminal on stdin and stdout, `fix` opens the editor on a temporary file holding the current source and compiles it on exit. After a compile error it asks `re-edit? [Y/n]` and reopens with the edits kept; saving an empty or unchanged file cancels. Embedders call `cc_debug_fix(vm, name, source)`.

| Rule | Detail |
|---|---|
| Signature | Return type, parameters and `static` must match the old definition |
| Scope | Functions of the last file only; the new source sees that file's declarations |
| Source lines | The new body belongs to the file `<repl>`: `break <repl>:2` |
| Breakpoints | `break <function>` follows the new body. A `file:line` breakpoint inside the old body is removed with a message; set it again on `<repl>:line` |
| Macros | The last file's macros expand; `#` directives are rejected, and a macro the file later `#undef`s is unavailable |
| Statics | A block-scope `static` keeps its storage when the new body declares one of the same name and type |

## Reloading a file

`reload <file>` re-reads an edited source file of the paused program (headers included) and redefines every function whose body changed.

```
(cccc-dbg) break step
(cccc-dbg) reload prog.c
Breakpoint #0 moved to PC 258
reloaded prog.c: 2 changed, 1 unchanged, 0 new
(cccc-dbg) continue
```

| Part of the file | After a reload |
|---|---|
| Changed function | Replaced as by `fix` |
| Unchanged function | Not recompiled; code, breakpoints and line numbers stay |
| New function or global | Added and callable; a global runs its initializer |
| Global, file-scope `static`, block-scope `static` | Keep storage and value; an edited initializer is ignored |
| Definition removed | Stays, with a note |

A function counts as changed when its expanded tokens differ, so editing a macro or header it uses changes it too. Macros and include guards are read afresh from the file; the session's macros are unaffected.

The reload is refused, with nothing changed, if the file:

- does not parse, or is not a file of the program
- changes a function's signature (return type, parameters, `static`)
- changes the layout of a struct, union, enum or typedef it defines, or the type of a global
- changes the value of a `constexpr` object
- has a comptime function or variable that does not compile[^comptime-reload]

Embedders call `cc_debug_reload(vm, path)`. `cccc --watch` runs this reload on every save without the debugger ([watch.md](watch.md)).

### Comptime code

A new or changed function may call a comptime function, in `reload` and `fix`; the call is expanded when the change is applied. `reload` also accepts edits to comptime code (`fix` does not):

| Comptime part | After a reload |
|---|---|
| New function or variable | Added and initialized |
| Changed function | Replaced; `static` locals matching by name and type keep their values |
| Variable with an edited declaration | Initialized again |
| Unchanged variable | Keeps its value |
| Function that calls comptime code, in the reloaded file | Expanded again, even if its own text is unchanged |
| Same, in another file | Keeps its old expansion; reload that file |
| Comptime code removed | Stays, with no note |
| File-scope comptime call (`gen();`) | Runs again[^file-scope-calls] |

[^comptime-reload]: Expansion regenerates the comptime code above the running program, runs it on top of the paused stack with the debugger off, and gives the code back before the new functions are compiled. A reload that changes comptime code builds the comptime program again the same way and runs only the initializers that are new or edited; an initializer that already ran is not undone when the reload is refused later. A comptime global, static, struct member or array element of function-pointer type that holds a comptime function follows the regenerated code. A pointer kept in heap memory or in a `void *` or integer is not moved.
[^file-scope-calls]: Every file-scope call in the reloaded file runs, so its side effects repeat on each reload. A function it generates replaces the live function of the same name, even when the text is identical, and must keep that function's signature. A generated function that a later reload no longer produces stays, with a note. A generator that picks names from a counter produces new names each time.
