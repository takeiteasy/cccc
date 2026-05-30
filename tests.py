#!/usr/bin/env python3
"""Test runner for JCC.

Runs all test_*.c files in tests/ directory and reports results.
Supports parallel execution with -j/--jobs.
"""

import argparse
import concurrent.futures
import fnmatch
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


def detect_platform():
    system = os.uname().sysname if hasattr(os, "uname") else os.name
    if system == "Darwin":
        return "macos"
    elif system == "Linux":
        return "linux"
    elif system in ("CYGWIN", "MINGW", "MSYS", "Windows"):
        return "windows"
    else:
        return "unknown"


def run_single_test(idx, test_file, jcc, script_dir, use_leaks, platform, jcc_args):
    tests_dir = Path(script_dir) / "tests"
    test_name = str(test_file.relative_to(tests_dir))

    is_negative_test = False
    expects_runtime_error = False
    try:
        with open(test_file, "r") as f:
            first_line = f.readline()
            if "EXPECT_COMPILE_ERROR" in first_line:
                is_negative_test = True
            if "EXPECT_RUNTIME_ERROR" in first_line:
                expects_runtime_error = True
    except Exception:
        pass

    if use_leaks:
        if platform == "macos":
            normal_cmd = [str(jcc), "-I./include", *jcc_args, str(test_file)]
            normal_result = subprocess.run(
                normal_cmd, capture_output=True, text=True, cwd=script_dir
            )
            leak_cmd = [
                "leaks",
                "-atExit",
                "--",
                str(jcc),
                "-I./include",
                *jcc_args,
                str(test_file),
            ]
            leak_result = subprocess.run(
                leak_cmd, capture_output=True, text=True, cwd=script_dir
            )
            output = (
                normal_result.stdout
                + normal_result.stderr
                + leak_result.stdout
                + leak_result.stderr
            )
            exit_code = normal_result.returncode
            leak_output = leak_result.stdout + leak_result.stderr
            is_leaking = "0 leaks" not in leak_output
            cmd = None
        elif platform == "linux":
            cmd = [
                "valgrind",
                "--leak-check=full",
                "--error-exitcode=1",
                "--quiet",
                str(jcc),
                "-I./include",
                *jcc_args,
                str(test_file),
            ]
        elif platform == "windows":
            cmd = [
                "drmemory",
                "-batch",
                "-quiet",
                "--",
                str(jcc),
                "-I./include",
                *jcc_args,
                str(test_file),
            ]
        else:
            cmd = [str(jcc), "-I./include", *jcc_args, str(test_file)]
    else:
        cmd = [str(jcc), "-I./include", *jcc_args, str(test_file)]

    if cmd is not None:
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=script_dir)
        output = result.stdout + result.stderr
        exit_code = result.returncode

        is_leaking = False
        if use_leaks and platform == "linux":
            if (
                "no leaks are possible" in output
                or "All heap blocks were freed" in output
            ):
                is_leaking = False
            elif (
                "definitely lost" in output
                or "indirectly lost" in output
                or "possibly lost" in output
            ):
                is_leaking = True
            else:
                is_leaking = False
        elif use_leaks and platform == "windows":
            if re.search(r"0 unique,.*0 total", output):
                is_leaking = False
            elif "LEAK" in output or "UNADDRESSABLE ACCESS" in output:
                is_leaking = True
            else:
                is_leaking = False

    has_compile_error = (
        "error generated" in output
        or "cannot open file" in output
        or "undefined function" in output
        or "implicit declaration of a function" in output
        or ("expected" in output and "got" in output)
    )

    crashed = exit_code in (134, 139, 136, 141, -6, -11, -8, -13)

    if crashed:
        status = "crashed"
    elif is_leaking:
        status = "leak"
    elif has_compile_error:
        if is_negative_test:
            status = "negative_pass"
        else:
            status = "compile_error"
    elif exit_code == 42:
        status = "passed"
    elif expects_runtime_error and exit_code == 255:
        status = "negative_pass"
    else:
        status = "failed"

    return {
        "idx": idx,
        "test_name": test_name,
        "exit_code": exit_code,
        "status": status,
        "output": output,
        "is_negative_test": is_negative_test,
        "expects_runtime_error": expects_runtime_error,
    }


def main():
    parser = argparse.ArgumentParser(description="Test runner for JCC")
    parser.add_argument(
        "--leaks", action="store_true", help="Enable memory leak detection"
    )
    parser.add_argument("--match", help="Filter tests by pattern")
    parser.add_argument(
        "-j", "--jobs", type=int, default=8, help="Number of parallel jobs"
    )
    args, jcc_args = parser.parse_known_args()

    script_dir = Path(__file__).parent.resolve()
    jcc = script_dir / "jcc"
    tests_dir = script_dir / "tests"

    if not jcc.exists():
        print("Error: jcc executable not found. Please run 'make' first.")
        sys.exit(1)

    if not tests_dir.exists():
        print("Error: tests directory not found.")
        sys.exit(1)

    platform = detect_platform()

    test_files = sorted(
        f for f in tests_dir.rglob("test_*.c")
        if "failures" not in f.parts
    )

    if args.match:
        test_files = [f for f in test_files if fnmatch.fnmatch(f.name, args.match)]

    if not test_files:
        print(f"No test files found in {tests_dir}")
        sys.exit(1)

    if args.jobs is not None:
        n_jobs = args.jobs
    else:
        n_jobs = os.cpu_count() or 1

    use_leaks = args.leaks
    if use_leaks and platform == "unknown":
        print("Warning: Memory leak detection not supported on this platform")
        use_leaks = False

    print("Running JCC tests...")
    if use_leaks:
        leak_tools = {"macos": "leaks", "linux": "valgrind", "windows": "drmemory"}
        print(
            f"Memory leak detection enabled (using '{leak_tools.get(platform, '?')}')"
        )
    if args.match:
        print(f"Filtering tests matching: {args.match}")
    print(f"Using {n_jobs} parallel jobs")
    print("=======================")
    print()

    test_args = [
        (i, test_file, jcc, str(script_dir), use_leaks, platform, jcc_args)
        for i, test_file in enumerate(test_files)
    ]

    results = [None] * len(test_files)
    next_to_print = 0
    results_lock = threading.Lock()

    total = 0
    passed = 0
    failed = 0
    crashed = 0
    negative_passed = 0
    failed_tests = []
    crashed_tests = []

    def print_single_result(result):
        nonlocal total, passed, failed, crashed, negative_passed
        total += 1
        test_name = result["test_name"]
        status = result["status"]
        exit_code = result["exit_code"]
        output = result["output"]

        if status == "crashed":
            crashed += 1
            crashed_tests.append(f"{test_name} (exit code: {exit_code})")
            print(f"💥 {test_name} (CRASHED: exit code {exit_code})")
        elif status == "compile_error":
            failed += 1
            failed_tests.append(f"{test_name} (COMPILATION ERROR)")
            print(f"✗ {test_name} (COMPILATION ERROR)")
            for line in output.splitlines()[:3]:
                print(f"  {line}")
        elif status == "leak":
            failed += 1
            failed_tests.append(f"{test_name} (MEMORY LEAK)")
            print(f"💧 {test_name} (MEMORY LEAK)")
            leak_lines = [line for line in output.splitlines() if "Leak:" in line][:3]
            for line in leak_lines:
                print(f"  {line}")
        elif status == "negative_pass":
            negative_passed += 1
            if result["is_negative_test"]:
                print(f"✓ {test_name} (correctly rejected invalid code)")
            else:
                print(f"✓ {test_name} (correctly detected runtime error)")
        elif status == "passed":
            passed += 1
            print(f"✓ {test_name}")
        elif status == "failed":
            failed += 1
            failed_tests.append(f"{test_name} (exit code: {exit_code})")
            print(f"✗ {test_name} (expected exit code 42, got: {exit_code})")

    def flush_results():
        nonlocal next_to_print
        while next_to_print < len(results) and results[next_to_print] is not None:
            print_single_result(results[next_to_print])
            next_to_print += 1

    def on_done(future, idx):
        try:
            result = future.result()
        except Exception as e:
            tests_dir = Path(script_dir) / "tests"
            result = {
                "idx": idx,
                "test_name": str(test_files[idx].relative_to(tests_dir)),
                "exit_code": -1,
                "status": "crashed",
                "output": str(e),
                "is_negative_test": False,
                "expects_runtime_error": False,
            }

        with results_lock:
            results[idx] = result
            flush_results()

    with concurrent.futures.ThreadPoolExecutor(max_workers=n_jobs) as executor:
        futures = []
        for arg in test_args:
            future = executor.submit(run_single_test, *arg)
            future.add_done_callback(lambda f, idx=arg[0]: on_done(f, idx))
            futures.append(future)
        concurrent.futures.wait(futures)

    def run_extra_regression(name, fn, negative=False):
        try:
            ok, output = fn()
        except Exception as e:
            ok = False
            output = str(e)

        return {
            "idx": len(results),
            "test_name": name,
            "exit_code": 42 if ok else 1,
            "status": "negative_pass"
            if ok and negative
            else ("passed" if ok else "failed"),
            "output": output,
            "is_negative_test": negative,
            "expects_runtime_error": False,
        }

    def hashmap_tombstone_regression():
        src = "\n".join(
            f"#define M{i} {i}\n#undef M{i}" for i in range(1000)
        )
        src += "\nint main(){ return 42; }\n"
        result = subprocess.run(
            [str(jcc), "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        return result.returncode == 42, result.stdout + result.stderr

    def unknown_opcode_regression():
        with tempfile.TemporaryDirectory() as tmpdir:
            bc = Path(tmpdir) / "ok.jbc"
            bad = Path(tmpdir) / "bad.jbc"
            src = "int main(){ return 42; }\n"
            saved = subprocess.run(
                [str(jcc), "-o", str(bc), "-"],
                input=src,
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            if saved.returncode != 0:
                return False, saved.stdout + saved.stderr

            data = bytearray(bc.read_bytes())
            header_size = 4 + 4 + 4 + 8 + 8 + 8 + 8
            first_opcode = header_size + 8
            if first_opcode + 8 > len(data):
                return False, "bytecode file too small"
            data[first_opcode:first_opcode + 8] = (999999).to_bytes(
                8, "little", signed=True
            )
            bad.write_bytes(data)

            loaded = subprocess.run(
                [str(jcc), str(bad)],
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            output = loaded.stdout + loaded.stderr
            return loaded.returncode != 0 and "unknown opcode" in output, output

    def static_duplicate_regression():
        with tempfile.TemporaryDirectory() as tmpdir:
            a = Path(tmpdir) / "a.c"
            b = Path(tmpdir) / "b.c"
            main = Path(tmpdir) / "main.c"
            a.write_text("static int same(void){return 10;} int a(void){return same();}\n")
            b.write_text("static int same(void){return 32;} int b(void){return same();}\n")
            main.write_text("int a(void); int b(void); int main(){return a()+b();}\n")
            result = subprocess.run(
                [str(jcc), str(a), str(b), str(main)],
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            return result.returncode == 42, result.stdout + result.stderr

    def json_nested_struct_regression():
        src = (
            "struct Inner { int x; };\n"
            "struct Outer { struct Inner in; union U { int i; float f; } u; };\n"
            "struct Outer make_outer(struct Inner in);\n"
        )
        result = subprocess.run(
            [str(jcc), "--json", "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0:
            return False, output
        try:
            parsed = json.loads(result.stdout)
        except json.JSONDecodeError as e:
            return False, f"{e}\n{output}"
        return (
            "structs" in parsed
            and "unions" in parsed
            and len(parsed["structs"]) >= 2
            and len(parsed["unions"]) >= 1
        ), output

    def macro_hideset_stress_regression():
        src = "#define A(x) B(x)\n#define B(x) A(x)\nA(42)\nint main(){return 42;}\n"
        result = subprocess.run(
            [str(jcc), "-E", "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        return result.returncode == 0 and "42" in result.stdout, result.stdout + result.stderr

    def optimizer_jump_target_regression():
        src = (
            "int main(){\n"
            "  int x = 0;\n"
            "  goto hop;\n"
            "hop:\n"
            "  goto done;\n"
            "done:\n"
            "  x = 42;\n"
            "  return x;\n"
            "}\n"
        )
        result = subprocess.run(
            [str(jcc), "--optimize=2", "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        return result.returncode == 42, result.stdout + result.stderr

    def bytecode_save_load_regression():
        with tempfile.TemporaryDirectory() as tmpdir:
            bc = Path(tmpdir) / "test.jbc"
            # Use a switch statement to exercise control flow opcodes
            src = (
                "int main(){\n"
                "  int x = 2;\n"
                "  int r;\n"
                "  switch(x){\n"
                "    case 1: r = 10; break;\n"
                "    case 2: r = 42; break;\n"
                "    case 3: r = 30; break;\n"
                "    default: r = 0; break;\n"
                "  }\n"
                "  return r;\n"
                "}\n"
            )
            saved = subprocess.run(
                [str(jcc), "-o", str(bc), "-"],
                input=src,
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            if saved.returncode != 0:
                return False, saved.stdout + saved.stderr
            if not bc.exists():
                return False, "bytecode file not created"
            loaded = subprocess.run(
                [str(jcc), str(bc)],
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            return loaded.returncode == 42, loaded.stdout + loaded.stderr

    def bytecode_jmpt_load_regression():
        with tempfile.TemporaryDirectory() as tmpdir:
            bc = Path(tmpdir) / "jmpt.jbc"
            import struct

            # Opcode values from jcc.h OPS_X enum
            JMPT = 3
            LI3 = 46
            LEV3 = 58

            # Build a minimal valid bytecode file with JMPT in dead code.
            # main_offset=8 means VM starts at text_seg[8] (LI3 r0, 42; LEV3).
            # JMPT at text_seg[1] is never executed but must be parsed by loader.
            header = b"JCC\x00"
            header += struct.pack("<i", 1)       # version
            header += struct.pack("<I", 0)       # flags
            header += struct.pack("<q", 96)      # text_size (12 qwords)
            header += struct.pack("<q", 0)       # data_size
            header += struct.pack("<q", 8)       # main_offset
            header += struct.pack("<q", 0)       # data_reloc_count

            text = b""
            text += struct.pack("<q", 8)          # qword 0: main_offset
            text += struct.pack("<q", JMPT)      # qword 1: JMPT opcode
            text += struct.pack("<q", 6)          # qword 2: table_addr (relative offset)
            text += struct.pack("<q", 1)          # qword 3: count
            text += struct.pack("<q", 7)          # qword 4: default_addr (relative offset)
            text += struct.pack("<q", 0)          # qword 5: padding
            text += struct.pack("<q", 0)          # qword 6: jump table entry
            text += struct.pack("<q", 0)          # qword 7: padding
            text += struct.pack("<q", LI3)       # qword 8: LI3
            text += struct.pack("<q", 0)          # qword 9: rd=0
            text += struct.pack("<q", 42)         # qword 10: immediate=42
            text += struct.pack("<q", LEV3)      # qword 11: LEV3

            bc.write_bytes(header + text)

            # Load and disassemble to verify the loader handles JMPT
            loaded = subprocess.run(
                [str(jcc), "-d", str(bc)],
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            output = loaded.stdout + loaded.stderr
            # Should not crash; exit code 0 means load succeeded
            return loaded.returncode == 0 and "Disassembly" in output, output

    def run_debugger_script(src, commands):
        with tempfile.TemporaryDirectory() as tmpdir:
            cfile = Path(tmpdir) / "debugger_condition.c"
            cfile.write_text(src)
            result = subprocess.run(
                [str(jcc), "-g", "-I./include", str(cfile)],
                input=commands,
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            return result.returncode, result.stdout + result.stderr

    def debugger_condition_expr_regression():
        src = (
            "struct S { int x; };\n"
            "struct S s = {21};\n"
            "int *p = &s.x;\n"
            "int g;\n"
            "int main(void) { return g ? 42 : 1; }\n"
        )
        commands = (
            "break main if (g = ((s.x << 1) == 42 && *p == 21 && "
            "&s.x != 0 && (s.x > 20 ? 1 : 0) && (0, s.x >= 21)), 0)\n"
            "continue\n"
        )
        code, output = run_debugger_script(src, commands)
        return code == 42, output

    def debugger_condition_assignment_regression():
        src = "int g; int main(void) { return g ? 42 : 1; }\n"
        code, output = run_debugger_script(
            src, "break main if (g = 1, 0)\ncontinue\n"
        )
        return code == 42, output

    def debugger_condition_funcall_regression():
        src = (
            "int g;\n"
            "int answer(int x) { return x + 1; }\n"
            "int main(void) { return g ? 42 : 1; }\n"
        )
        code, output = run_debugger_script(
            src, "break main if (g = (answer(41) == 42), 0)\ncontinue\n"
        )
        return code == 42, output

    def debugger_condition_full_abi_reject_regression():
        src = (
            "int takes_double(double x) { return 1; }\n"
            "int main(void) { return 42; }\n"
        )
        code, output = run_debugger_script(
            src, "break main if takes_double(1.0)\ncontinue\n"
        )
        return (
            code == 42
            and "Non-integer function arguments in conditions are not supported"
            in output
        ), output

    def text_segment_overflow_regression():
        body = "\n".join(f"x = x + {i};" for i in range(15000))
        src = f"int main() {{ int x = 0; {body} return x; }}\n"
        result = subprocess.run(
            [str(jcc), "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        output = result.stdout + result.stderr
        return (
            result.returncode != 0 and "text segment overflow" in output
        ), output

    def data_segment_overflow_regression():
        body = "\n".join(
            f"double d{i} = {i}.5;" for i in range(40000)
        )
        src = f"{body}\nint main() {{ return 42; }}\n"
        result = subprocess.run(
            [str(jcc), "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        output = result.stdout + result.stderr
        return (
            result.returncode != 0 and "data segment overflow" in output
        ), output

    for extra in [
        run_extra_regression(
            "generated_hashmap_tombstones", hashmap_tombstone_regression
        ),
        run_extra_regression(
            "generated_unknown_opcode_bytecode", unknown_opcode_regression
        ),
        run_extra_regression(
            "generated_static_duplicate_symbols", static_duplicate_regression
        ),
        run_extra_regression(
            "generated_json_nested_structs", json_nested_struct_regression
        ),
        run_extra_regression(
            "generated_macro_hideset_stress", macro_hideset_stress_regression
        ),
        run_extra_regression(
            "generated_optimizer_jump_target", optimizer_jump_target_regression
        ),
        run_extra_regression(
            "generated_bytecode_save_load", bytecode_save_load_regression
        ),
        run_extra_regression(
            "generated_bytecode_jmpt_load", bytecode_jmpt_load_regression
        ),
        run_extra_regression(
            "generated_debugger_condition_expr",
            debugger_condition_expr_regression,
        ),
        run_extra_regression(
            "generated_debugger_condition_assignment",
            debugger_condition_assignment_regression,
        ),
        run_extra_regression(
            "generated_debugger_condition_funcall",
            debugger_condition_funcall_regression,
        ),
        run_extra_regression(
            "generated_debugger_condition_full_abi_reject",
            debugger_condition_full_abi_reject_regression,
        ),
        run_extra_regression(
            "generated_text_segment_overflow",
            text_segment_overflow_regression,
            negative=True,
        ),
        run_extra_regression(
            "generated_data_segment_overflow",
            data_segment_overflow_regression,
            negative=True,
        ),
    ]:
        print_single_result(extra)

    print()
    print("=======================")
    print("Test Results Summary")
    print("=======================")
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

    if failed > 0 or crashed > 0:
        sys.exit(1)
    else:
        print()
        print("All tests passed! 🎉")
        sys.exit(0)


if __name__ == "__main__":
    main()
