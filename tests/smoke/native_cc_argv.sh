# The argv -c=native hands to the host compiler: -lm, a probed -std=, defines
# and -fsigned-char reach it, a --std= the host rejects is an error, and a
# pre-C11 --std= is floored to C11.
. "$(dirname "$0")/lib.sh"

echo 'int main(void) { return 42; }' >prog.c
printf 'struct point { int x, y; };\nint main(void) { struct point p = {40, 2}; return p.x + p.y; }\n' >point.c

# new_log: start a fresh argv log for the next compile.
new_log() { rm -f argv.log; : >argv.log; }

# argv_has LINE: LINE is one of the recorded host compiler arguments.
argv_has() { grep -qxF -- "$1" argv.log; }

# Always links -lm.
new_log
logging_wrapper w.sh "$TMP/argv.log"
CCCC_NATIVE_CC="$TMP/w.sh" expect_exit 0 "$CCCC" -c=native -o lm_out prog.c
argv_has -lm || fail "-lm missing from the host compiler argv: $(cat argv.log)"
expect_exit 42 ./lm_out

# Probes a -std= even when none was given.
new_log
CCCC_NATIVE_CC="$TMP/w.sh" expect_exit 0 "$CCCC" -c=native -o ladder_out prog.c
grep -q '^-std=gnu' argv.log || fail "no -std=gnu* forwarded: $(cat argv.log)"
expect_exit 42 ./ladder_out

# An explicit --std=c23 falls back to the gnu2x spelling when -std=c23 and
# -std=gnu23 are rejected.
new_log
logging_wrapper w23.sh "$TMP/argv.log" 'for a in "$@"; do
  if [ "$a" = "-std=c23" ] || [ "$a" = "-std=gnu23" ]; then exit 1; fi
done'
CCCC_NATIVE_CC="$TMP/w23.sh" expect_exit 0 "$CCCC" --std=c23 -c=native -o std_out prog.c
argv_has -std=gnu2x || fail "-std=gnu2x not forwarded: $(cat argv.log)"
expect_exit 42 ./std_out

# A host that rejects every -std= makes an explicit --std= an error; without
# one the build still works.
new_log
logging_wrapper wrej.sh "$TMP/argv.log" 'for a in "$@"; do
  case "$a" in -std=*) exit 1;; esac
done'
CCCC_NATIVE_CC="$TMP/wrej.sh" expect_fail "$CCCC" --std=c23 -c=native -o rej_bad prog.c
grep -q -- '--std=' err.txt || { cat err.txt >&2; fail "expected a --std= diagnostic"; }
CCCC_NATIVE_CC="$TMP/wrej.sh" expect_exit 0 "$CCCC" -c=native -o rej_ok prog.c
expect_exit 42 ./rej_ok

# --std=c99 is floored to C11 for the emitted file.
new_log
CCCC_NATIVE_CC="$TMP/w.sh" expect_exit 0 "$CCCC" --std=c99 -c=native -o floor_out point.c
for s in -std=gnu99 -std=c99 -std=gnu9x -std=c9x; do
    argv_has "$s" && fail "pre-C11 $s forwarded: $(cat argv.log)"
done
argv_has -std=gnu11 || argv_has -std=gnu1x || argv_has -std=c11 || argv_has -std=c1x ||
    fail "no C11 -std= forwarded: $(cat argv.log)"
expect_exit 42 ./floor_out

# -D options survive.
new_log
CCCC_NATIVE_CC="$TMP/w.sh" expect_exit 0 "$CCCC" -c=native -D A=1 -D B=2 -o def_out prog.c
argv_has -DA=1 || fail "-DA=1 missing: $(cat argv.log)"
argv_has -DB=2 || fail "-DB=2 missing: $(cat argv.log)"

# -fsigned-char is forwarded.
new_log
CCCC_NATIVE_CC="$TMP/w.sh" expect_exit 0 "$CCCC" -c=native -o char_out prog.c
argv_has -fsigned-char || fail "-fsigned-char missing: $(cat argv.log)"
