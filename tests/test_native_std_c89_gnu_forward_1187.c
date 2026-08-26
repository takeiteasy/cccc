// CCCC_FLAGS: --std=c89
//
// #1187: native_resolve_std_ladder() (src/main.c) used to forward the
// literal std prefix the user typed -- `--std=c89` (no `gnu`) forwarded
// real `-std=c89` to the host compiler, strict ISO C90 with no GNU/C99
// extensions even as warnings. CCCC's own frontend only pedantic-warns on
// exactly the constructs below under `--std=c89` (it does not error), so a
// real host GCC's strict `-std=c89` rejected a program that compiled and
// ran fine under the VM. Fixed by always probing `gnu<NN>` before `c<NN>`
// (CCCC's frontend treats them identically -- c_std_gnu has no other
// reader), restoring "VM passes => native passes".
//
// This test's teeth are GCC-side: Apple clang already accepts every
// construct below under plain `-std=c89` as a warning (not an error), so
// on macOS with the default clang-backed CCCC_NATIVE_CC this test passes
// even without the fix -- it only catches a regression on a real GCC host
// (verified with /opt/homebrew/bin/gcc-16 locally; sr.ht's Linux CI runs
// real GCC too). See tests/suites/test_suite_std_c89.c for the fuller
// per-construct suite this mirrors; this file exists as a native-focused,
// single-TU repro of the exact divergence #1187 reported.

struct point {
    int x, y;
};

int main(void) {
    int n     = 3;
    int total = 0;
    int i;

    for (i = 0; i < n; i++) {
        int arr[n]; // VLA -- C99 extension
        arr[i]  = i;
        total  += arr[i];
    }

    {
        struct point p  = (struct point){1, 2}; // compound literal
        struct point q  = {.x = 1, .y = 2};     // designated initializer
        total          += p.x + q.y;
    }

    total        += 1; // statement before the mixed-decl int below
    int extra     = 1; // mixed declarations and code -- C99 extension
    total        += extra;

    long long ll  = 1; // 'long long' -- C99 extension
    total        += (int)ll;

    // a line comment -- C99 extension under strict ISO C90

    return total == 9 ? 42 : 1;
}
