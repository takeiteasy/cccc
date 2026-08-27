"""Result reporting: per-suite summary and benchmark block."""


def print_summary(r, args):
    """Print the final summary block for a single test suite run.

    r      — dict returned by _run_test_suite
    args   — argparse namespace (or SimpleNamespace) with bench, quiet attrs
    """
    total = r["total"]
    passed = r["passed"]
    failed = r["failed"]
    crashed = r["crashed"]
    negative_passed = r["negative_passed"]
    native_passed = r.get("native_passed", 0)
    native_failed = r.get("native_failed", 0)
    native_skipped = r.get("native_skipped", 0)
    native_compile_failed = r.get("native_compile_failed", 0)
    failed_tests = r["failed_tests"]
    crashed_tests = r["crashed_tests"]
    native_skipped_tests = r.get("native_skipped_tests", [])
    timings = r["timings"]

    native_mode = getattr(args, "native", False)

    print()
    print("=======================")
    print("Test Results Summary")
    print("=======================")
    if native_mode:
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
    if native_mode:
        print()
        print(f"All {native_passed} native roundtrips passed ({native_skipped} skipped).")
    else:
        print()
        print("All tests passed! 🎉")
    return True

