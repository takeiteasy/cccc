# The examples/readme/*.c programs shown in the README run in the VM and
# under -c=native, and the comptime example's test and build recipe work.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$(dirname "$0")/lib.sh"
EX=$ROOT/examples/readme

expect_exit 42 "$CCCC" "$EX/derive.c"
grep -q '^{"id":7,"name":"ann","favourite":1}$' out.txt || fail "derive.c JSON output"
grep -q '^GREEN$' out.txt || fail "derive.c enum string"

expect_exit 0 "$CCCC" "$EX/comptime.c"
grep -q '^256 32$' out.txt || fail "comptime.c output: $(cat out.txt)"

expect_exit 0 "$CCCC" --testing "$EX/comptime.c"
grep -q '^ok 1 - dot_product$' out.txt || fail "comptime.c test"

for name in derive comptime; do
    expect_exit 0 "$CCCC" -c=native -o "$name" "$EX/$name.c"
done
expect_exit 42 ./derive
grep -q '^GREEN$' out.txt || fail "native derive output"
expect_exit 0 ./comptime
grep -q '^256 32$' out.txt || fail "native comptime output"

cp "$EX/comptime.c" .
expect_exit 0 "$CCCC" --build comptime.c
expect_exit 0 build/bin/demo
grep -q '^256 32$' out.txt || fail "built demo output"
