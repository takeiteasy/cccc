#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -f "./jcc" ]; then
    echo "Building jcc..." >&2
    make
fi

echo "Generating src/std.c..." >&2

{
    echo '#include <string.h>'
    echo
    ./jcc -M -I./include std_template_test.c \
        | awk '/^char \*.*get_std_header/,/^\}$/'
} > src/std.c

echo "Done. Rebuild with: make" >&2
