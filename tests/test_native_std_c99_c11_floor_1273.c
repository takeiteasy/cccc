// CCCC_FLAGS: --std=c99
//
// #1273: native_resolve_std_ladder() (now cccc_resolve_host_std(), src/exec.c)
// used to forward a spelling of whatever standard the user *named* -- under
// `--std=c99` that meant `-std=gnu99`/`-std=c99` was handed to the host --
// even though the -c=native serializer emits a fixed GNU C11 floor
// unconditionally (_Static_assert/_Alignof layout guards for every struct
// definition, among others -- see man/NATIVE.md), regardless of --std=. A
// plain struct is enough to trigger those guards, no explicit C11 construct
// needed in the source at all, so a real host (`-std=gnu99` on GCC, or a
// strict `-std=c99`) rejected the emitted file even though the identical
// program compiled and ran fine both under the VM and under -c=native with
// no --std= at all.
//
// Fixed by flooring the probed spelling at C11: a --std= naming a pre-C11
// standard now resolves to a C11 spelling (gnu11/gnu1x/c11/c1x) instead,
// since the emitted file genuinely is C11 -- promoting the forwarded
// spelling describes it honestly rather than mis-describing it as the
// older standard the user named.

struct point {
    int x, y;
};

int main(void) {
    struct point p = {40, 2};
    return p.x + p.y;
}
