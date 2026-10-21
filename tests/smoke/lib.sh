# Shared by tests/smoke/*.sh.  Usage: sh tests/smoke/<name>.sh <path-to-cccc>
#
# Runs in a scratch directory (nothing is written into the repository) and
# exits non-zero on the first failed check.

CCCC=$1
case $CCCC in
    /*) ;;
    *) CCCC=$(pwd)/$CCCC ;;
esac
[ -x "$CCCC" ] || { echo "smoke: no cccc at $1" >&2; exit 2; }

export CCCC_NO_CONFIG=1
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cccc-smoke.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 2

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# expect_exit CODE CMD...: run CMD with stdin closed; fail unless it exits CODE.
expect_exit() {
    want=$1
    shift
    "$@" </dev/null >out.txt 2>err.txt
    got=$?
    [ "$got" -eq "$want" ] || {
        cat out.txt err.txt >&2
        fail "'$*' exited $got, expected $want"
    }
}

# expect_fail CMD...: run CMD with stdin closed; fail if it exits 0.
expect_fail() {
    if "$@" </dev/null >out.txt 2>err.txt; then
        cat out.txt err.txt >&2
        fail "'$*' unexpectedly succeeded"
    fi
}

# real_cc: the host C compiler (cc, clang or gcc).
real_cc() {
    for c in cc clang gcc; do
        if command -v "$c" >/dev/null 2>&1; then
            command -v "$c"
            return 0
        fi
    done
    fail "no host C compiler"
}

# cc_family CC: prints clang, gcc or unknown from CC's predefined macros.
cc_family() {
    defs=$("$1" -dM -E - </dev/null 2>/dev/null) || { echo unknown; return; }
    case $defs in
        *__clang__*) echo clang ;;
        *__GNUC__*) echo gcc ;;
        *) echo unknown ;;
    esac
}

# find_family WANT NAME...: the first NAME on PATH whose cc_family is WANT.
find_family() {
    want=$1
    shift
    for n in "$@"; do
        p=$(command -v "$n" 2>/dev/null) || continue
        [ "$(cc_family "$p")" = "$want" ] && { echo "$p"; return 0; }
    done
    return 1
}

# logging_wrapper PATH LOG [EXTRA]: a CCCC_NATIVE_CC that appends its argv, one
# per line, to LOG and then runs the host compiler.  EXTRA is shell inserted
# before the exec (e.g. a rejection rule).
logging_wrapper() {
    cc=$(real_cc)
    {
        echo '#!/bin/sh'
        echo "printf '%s\\n' \"\$@\" >> '$2'"
        [ -n "$3" ] && echo "$3"
        echo "exec '$cc' \"\$@\""
    } >"$1"
    chmod +x "$1"
}
