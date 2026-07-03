"""Per-pass optimization matrix sweep.

Replaces the old -O0..-O4 sweep (--full) with a 9-run -f pass matrix:
one baseline, one run per individual pass, and one stress run (all passes on).
Each run uses -O0 plus the specific -f flag(s) so the optimizer state is clean.

When a test has CCCC_FLAGS containing -On, that flag is stripped in matrix mode
(run_single_test matrix_mode=True) so the injected -O0 governs and the
per-pass attribution is accurate.
"""

import types

from .suite import _run_test_suite
from .report import print_matrix_summary

# The 7 individual optimization passes in order.
PASSES = [
    ("-ffold",      "constant folding"),
    ("-fpeephole",  "peephole"),
    ("-fcopy-prop", "copy propagation"),
    ("-fdce",       "dead code elimination"),
    ("-fcse",       "common subexpr elimination"),
    ("-ffuse",      "opcode fusion"),
    ("-felim-ext",  "extension elimination"),
]

# 9 runs: baseline, one per pass, stress (all on)
MATRIX_RUNS = (
    [("baseline", "none",   ["-O0"])]
    + [(flag.lstrip("-"), name, ["-O0", flag]) for flag, name in PASSES]
    + [("stress",   "all passes", ["-O0"] + [f for f, _ in PASSES])]
)


def run_pass_matrix(cccc, script_dir, platform, base_cccc_args, n_jobs, args, test_files):
    """Run each optimization pass in isolation, then report attribution.

    Returns True if every run passed, False if any run had failures.
    """
    # Strip any -O/-On flags from the caller's cccc_args so they don't
    # collide with the matrix injections.
    filtered_base = []
    skip = False
    for a in base_cccc_args:
        if skip:
            skip = False
            continue
        if a in ("-O", "--optimize"):
            skip = True
            continue
        if a.startswith("-O") and len(a) > 2 and a[2].isdigit():
            continue
        if a.startswith("--optimize="):
            continue
        filtered_base.append(a)

    all_results = {}
    passes_display = []  # (label, name) pairs for the summary table

    for label, name, extra_flags in MATRIX_RUNS:
        run_cccc_args = filtered_base + extra_flags

        # Force quiet per-run output so the matrix doesn't flood the terminal;
        # the attribution summary is printed once after all runs.
        run_args = types.SimpleNamespace(
            vm_profile=getattr(args, "vm_profile", False),
            bench=False,
            c4=getattr(args, "c4", False),
            quiet=True,
            process_timeout=getattr(args, "process_timeout", None),
        )

        print(f"\n--- Pass: {label} ({name}) ---")
        r = _run_test_suite(
            cccc, script_dir, False, platform, run_cccc_args,
            n_jobs, run_args, test_files,
            matrix_mode=True,
        )
        all_results[label] = r
        passes_display.append((label, name))

        # Brief per-run line (totals only)
        r_passed = r["passed"] + r["negative_passed"]
        r_failed = r["failed"]
        r_crashed = r["crashed"]
        status_str = "PASS" if r_failed == 0 and r_crashed == 0 else "FAIL"
        print(f"    {r['total']} tests: {r_passed} passed, "
              f"{r_failed} failed, {r_crashed} crashed  [{status_str}]")

    return print_matrix_summary(all_results, passes_display)
