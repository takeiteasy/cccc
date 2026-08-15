"""Result reporting: per-suite summary and benchmark block."""


def print_summary(r, args):
    """Print the final summary block for a single test suite run.

    r      — dict returned by _run_test_suite
    args   — argparse namespace (or SimpleNamespace) with c4, bench, quiet attrs
    """
    total = r["total"]
    passed = r["passed"]
    failed = r["failed"]
    crashed = r["crashed"]
    negative_passed = r["negative_passed"]
    c4_passed = r["c4_passed"]
    c4_failed = r["c4_failed"]
    c4_skipped = r["c4_skipped"]
    c4_save_failed = r["c4_save_failed"]
    native_passed = r.get("native_passed", 0)
    native_failed = r.get("native_failed", 0)
    native_skipped = r.get("native_skipped", 0)
    native_compile_failed = r.get("native_compile_failed", 0)
    failed_tests = r["failed_tests"]
    crashed_tests = r["crashed_tests"]
    c4_skipped_tests = r["c4_skipped_tests"]
    native_skipped_tests = r.get("native_skipped_tests", [])
    timings = r["timings"]

    c4_mode = getattr(args, "c4", False)
    native_mode = getattr(args, "native", False)

    print()
    print("=======================")
    print("Test Results Summary")
    print("=======================")
    if c4_mode:
        print(f"Total:          {total}")
        print(f"C4 passed:      {c4_passed}")
        print(f"C4 skipped:     {c4_skipped}")
        print(f"C4 failed:      {c4_failed}")
        print(f"C4 save fail:   {c4_save_failed}")
    elif native_mode:
        print(f"Total:              {total}")
        print(f"Native passed:      {native_passed}")
        print(f"Native skipped:     {native_skipped}")
        print(f"Native failed:      {native_failed}")
        print(f"Native compile fail: {native_compile_failed}")
    else:
        print(f"Total:          {total}")
        print(f"Passed:         {passed}")
        print(f"Negative tests: {negative_passed} (correctly rejected invalid code)")
        print(f"Failed:         {failed}")
        print(f"Crashed:        {crashed}")

    if crashed > 0:
        print()
        print("⚠️  CRASHED TESTS (segfaults/aborts):")
        for test in crashed_tests:
            print(f"  - {test}")

    if failed > 0:
        print()
        print("Failed tests:")
        for test in failed_tests:
            print(f"  - {test}")

    if c4_mode and c4_skipped > 0 and not getattr(args, "quiet", False):
        print()
        print(f"C4 skipped tests ({c4_skipped}):")
        for test in c4_skipped_tests:
            print(f"  - {test}")

    if native_mode and native_skipped > 0 and not getattr(args, "quiet", False):
        print()
        print(f"Native skipped tests ({native_skipped}):")
        for test in native_skipped_tests:
            print(f"  - {test}")

    if getattr(args, "bench", False) and timings:
        print()
        print("=======================")
        print("Benchmark Results")
        print("=======================")
        timings_sorted = sorted(timings, key=lambda x: x[1], reverse=True)
        total_time = sum(t[1] for t in timings_sorted)
        avg_time = total_time / len(timings_sorted)
        print(f"Total time:     {total_time*1000:.1f}ms")
        print(f"Average/test:   {avg_time*1000:.1f}ms")
        print(f"Slowest test:   {timings_sorted[0][0]} ({timings_sorted[0][1]*1000:.1f}ms)")
        print(f"Fastest test:   {timings_sorted[-1][0]} ({timings_sorted[-1][1]*1000:.1f}ms)")
        print()
        print("Top 5 slowest tests:")
        for name, elapsed in timings_sorted[:5]:
            print(f"  {elapsed*1000:8.1f}ms  {name}")

    if failed > 0 or crashed > 0:
        return False
    if c4_mode:
        print()
        print(f"All {c4_passed} c4 roundtrips passed ({c4_skipped} skipped).")
    elif native_mode:
        print()
        print(f"All {native_passed} native roundtrips passed ({native_skipped} skipped).")
    else:
        print()
        print("All tests passed! 🎉")
    return True


def print_matrix_summary(all_results, passes, declared_result=None):
    """Print the per-pass attribution table for --matrix mode.

    all_results      — dict mapping pass_label → _run_test_suite result dict
    passes           — list of (pass_label, pass_name) pairs in display order
    declared_result  — optional _run_test_suite result dict for the extra
                        declared-level run of CCCC_MATRIX_SKIP tests (run at
                        their own -O level, outside the 9-pass attribution
                        grid since it isn't one of the -f passes)
    """
    print()
    print("========================================")
    print("Optimization Pass Matrix Summary")
    print("========================================")
    print(f"{'Pass':<22} {'Total':>6} {'Passed':>6} {'Failed':>6} {'Crashed':>6} {'Skipped':>7}")
    print("-" * 60)
    grand_total = grand_passed = grand_failed = grand_crashed = grand_skipped = 0
    for label, name in passes:
        r = all_results[label]
        r_total = r["total"]
        r_passed = r["passed"] + r["negative_passed"] + r["c4_passed"]
        r_failed = r["failed"]
        r_crashed = r["crashed"]
        r_skipped = r.get("matrix_skipped", 0)
        display = f"{label} ({name})"
        print(f"{display:<22} {r_total:>6} {r_passed:>6} {r_failed:>6} {r_crashed:>6} {r_skipped:>7}")
        grand_total += r_total
        grand_passed += r_passed
        grand_failed += r_failed
        grand_crashed += r_crashed
        grand_skipped += r_skipped
    print("-" * 60)
    print(f"{'Sum':<22} {grand_total:>6} {grand_passed:>6} {grand_failed:>6} {grand_crashed:>6} {grand_skipped:>7}")

    declared_failed = declared_crashed = 0
    if declared_result is not None:
        d_total = declared_result["total"]
        d_passed = declared_result["passed"] + declared_result["negative_passed"]
        declared_failed = declared_result["failed"]
        declared_crashed = declared_result["crashed"]
        print(f"{'declared (own -O)':<22} {d_total:>6} {d_passed:>6} {declared_failed:>6} {declared_crashed:>6} {0:>7}")
    print()

    # Per-test attribution: which pass(es) broke each test?
    per_test_passes = {}
    for label, _name in passes:
        r = all_results[label]
        for entry in r["failed_tests"] + r["crashed_tests"]:
            test_name = entry.split(" (")[0]
            per_test_passes.setdefault(test_name, set()).add(label)

    if per_test_passes:
        all_labels = {label for label, _ in passes}
        print("Failed Tests by Pass:")
        print("-" * 52)
        for test_name in sorted(per_test_passes):
            labels_set = per_test_passes[test_name]
            if labels_set == all_labels:
                pass_str = "all passes"
            else:
                pass_str = ", ".join(sorted(labels_set))
            print(f"  ✗ {test_name}  ({pass_str})")
        print()

    if declared_result is not None and (declared_failed or declared_crashed):
        print("Failed Tests (declared-level run):")
        print("-" * 52)
        for entry in declared_result["failed_tests"] + declared_result["crashed_tests"]:
            print(f"  ✗ {entry}")
        print()

    return grand_failed == 0 and grand_crashed == 0 and declared_failed == 0 and declared_crashed == 0
