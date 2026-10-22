# Feature-Test Operators

The `__has_*` preprocessor operators. Each is visible to `defined`, `#ifdef` and `#ifndef`, and also evaluates outside `#if`:

```c
#ifdef __has_builtin
int has_expect = __has_builtin(__builtin_expect);   // 1
#endif
```

| Operator | Returns |
|---|---|
| `__has_include`, `__has_include_next` | 1 if the file exists. `__has_include_next(<f.h>)` looks only in directories after the one the current file was found in (every directory for the main source file), as `#include_next` would |
| `__has_feature`, `__has_extension` | Support for `c99`, `c11`, `c23`, `c_alignas`, `c_alignof`, `c_generic_selections`, `c_static_assert`. Parsed-but-incomplete features (`_Atomic`, `_Thread_local`) return 0 |
| `__has_builtin` | 1 only for builtins with compiler semantics |
| `__has_attribute` | 1 for attributes with compiler semantics, and for GNU attributes that are inert on a bytecode VM but recognised by name, so headers that feature-test them agree with GCC and Clang ([attributes-ignored.md](attributes-ignored.md)). Unknown attributes return 0 |
| `__has_c_attribute` | The C23 version date (`202311L`) for standard C23 attributes; 1 for CCCC vendor attributes (including the [`<cccc/derive.h>`](derive.md) ones such as `cccc::accessors`, with or without the header); 1 for anything `__has_attribute` knows in the `gnu` or `__gnu__` scope; else 0 |
| `__has_cpp_attribute` | Always 0 |
| `__has_declspec_attribute`, `__has_warning` | Recognised so headers parse, but always 0: there are no `__declspec` attributes and no `-W`-name registry |

`error`, `warning`, `warn_unused_result` and `__warn_unused_result__` all return 1 from `__has_attribute`, since they carry real semantics.

Any other `__has_*`-shaped operator evaluates to 0 and emits a `-Wcpp` warning naming it, instead of a confusing "not a function" error.
