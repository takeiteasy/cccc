# Headers

How `#include` finds CCCC's bundled standard headers, your project's headers and the host SDK's.

## Quick start

```sh
cccc program.c                                  # <stdio.h>, <stdbool.h>, ... work from any directory
cccc -I./myproject/include program.c            # project headers still need -I
cccc --use-system-headers --sysroot "$(xcrun --show-sdk-path)" program.c   # real SDK headers
```

## Search order

For a standard header CCCC knows about:

| Step | Source | Notes |
|---|---|---|
| 1 | User `-I` paths | Always wins, even over a bundled copy |
| 2 | The embedded table (`src/std_table.c`) | Linked into the binary: no filesystem access, no working-directory dependency |
| 3 | `./include` on disk | Fallback for a header with no table row |
| 4 | System paths (`-i`/`--isystem`, `--sysroot`) | Subject to the flags below |

A header CCCC has never heard of (your `"local.h"`) skips steps 2 and 3 and resolves only through `-I` and system paths.

## Owned and known headers

| Class | Headers | Rule |
|---|---|---|
| **Known** | Roughly every C standard header plus some POSIX ones | CCCC has an embedded copy |
| **Owned** | `stdarg.h`, `setjmp.h`, `stdbool.h`, `stddef.h`, `stdint.h`, `inttypes.h`, `complex.h`, `stdatomic.h`, `stdckdint.h` | Tied to the VM's ABI and types, so they always resolve to CCCC's copy, whatever the flags |

## Flags

| Flag | Effect |
|---|---|
| `--use-system-headers` | For known, non-owned headers, prefer a real SDK copy before CCCC's own |
| `--sysroot <path>` | Adds `<path>/usr/include`, `<path>/usr/local/include` and `<path>/System/Library/Frameworks`; implies `--use-system-headers` |
| `--no-builtin-includes` | With `--use-system-headers` only: fail instead of falling back to CCCC's copy |
| `-F <dir>`, `--framework-path <dir>` | User framework path |
| `--iframework <dir>` | System framework path |

Real SDK headers need a few extra things; see [headers-sdk.md](headers-sdk.md).

## Framework headers (macOS)

`<Name/X.h>` resolves to `Name.framework/Headers/X.h`, then `Name.framework/PrivateHeaders/X.h`, under the framework paths:

```sh
cccc --sysroot "$(xcrun --show-sdk-path)" -E watcher.c    # <CoreServices/CoreServices.h> resolves
cccc -F ./vendor/Frameworks app.c                         # ./vendor/Frameworks/Foo.framework
```

Frameworks are searched after `-I` and `--isystem`, and a framework header also finds that framework's sub-frameworks (`X.framework/Frameworks/Name.framework`). `#include_next` and `__has_include_next` continue in the same order. Under `-c=native` the flags are forwarded as `-F` and `-iframework`.[^fw]

[^fw]: `-framework NAME` at link time is a separate option.

## Related

| Page | Covers |
|---|---|
| [headers-sdk.md](headers-sdk.md) | Using real SDK headers |
| [headers-native.md](headers-native.md) | Includes under `-c=native`, URL includes, bundled headers |
