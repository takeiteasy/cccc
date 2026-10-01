#!/usr/bin/env python3
"""OpenCL C dialect selection smoke tests.

`tools/tests.py` runs one `test_*.c` file per case, so it cannot drive the
parts of the OpenCL frontend that depend on the file name or on several inputs
at once. This script covers them through the real driver:

  1. a `.cl` file is read as OpenCL C with no flag
  2. `-x cl` reads a `.c` file as OpenCL C
  3. `-x c` turns that off for a `.cl` file
  4. OpenCL spellings in a plain `.c` file are an error
  5. `-cl-std=CL1.2` is accepted
  6. `-x` with an unknown language is a usage error
  7. a `.cl` input does not leak its keyword macros into a later `.c` input
  8. a `.cl` file builds and runs under `-c=native`
  9. the prelude is found from a directory that has no `include/`, in the VM
     and under `-c=native`

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

KERNEL_SRC = """\
__constant int scale = 2;

__kernel void twice(__global int *out, __global const int *in) {
    size_t i = get_global_id(0);
    out[i] = in[i] * scale;
}

int main(void) {
    int in[4] = {1, 2, 3, 4}, out[4];
    cccc_launch(twice, CCCC_RANGE(4), CCCC_RANGE(2), out, in);
    return out[0] + out[3] == 10 ? 42 : 1;
}
"""

# `local`, `global` and `kernel` are OpenCL keywords only inside an OpenCL input.
KERNEL_ONLY_SRC = "kernel void k(global int *p) { p[get_global_id(0)] = 1; }\n"
PLAIN_C_SRC = """\
int main(void) {
    int local = 3, global = 4, kernel = 35;
    return local + global + kernel;
}
"""


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=300)


def expect(name, ok, result):
    if ok:
        print(f"  ok: {name}")
        return True
    print(f"  FAIL: {name}\n    rc={result.returncode}\n    stderr={result.stderr[-400:]!r}")
    return False


def write(tmp, name, text):
    path = Path(tmp) / name
    path.write_text(text)
    return str(path)


def case_cl_extension(cccc, tmp, root):
    src = write(tmp, "ext.cl", KERNEL_SRC)
    r = run([str(cccc), f"-I{root}/include", src], root)
    return expect(".cl is read as OpenCL C", r.returncode == 42, r)


def case_x_cl(cccc, tmp, root):
    src = write(tmp, "as_c.c", KERNEL_SRC)
    r = run([str(cccc), f"-I{root}/include", "-x", "cl", src], root)
    return expect("-x cl on a .c file", r.returncode == 42, r)


def case_x_c(cccc, tmp, root):
    src = write(tmp, "off.cl", KERNEL_SRC)
    r = run([str(cccc), f"-I{root}/include", "-x", "c", src], root)
    return expect("-x c on a .cl file is an error", r.returncode != 0 and "error" in r.stderr, r)


def case_c_rejects_keywords(cccc, tmp, root):
    src = write(tmp, "plain.c", KERNEL_SRC)
    r = run([str(cccc), f"-I{root}/include", src], root)
    return expect("OpenCL spellings in a .c file are an error", r.returncode != 0 and "error" in r.stderr, r)


def case_cl_std(cccc, tmp, root):
    src = write(tmp, "std.cl", KERNEL_SRC)
    r = run([str(cccc), f"-I{root}/include", "-cl-std=CL1.2", src], root)
    return expect("-cl-std=CL1.2 is accepted", r.returncode == 42 and "warning" not in r.stderr, r)


def case_unknown_language(cccc, tmp, root):
    src = write(tmp, "lang.cl", KERNEL_SRC)
    r = run([str(cccc), "-x", "fortran", src], root)
    return expect("-x with an unknown language", r.returncode != 0 and "unknown language" in r.stderr, r)


def case_no_macro_leak(cccc, tmp, root):
    cl = write(tmp, "leak.cl", KERNEL_ONLY_SRC)
    c = write(tmp, "leak.c", PLAIN_C_SRC)
    r = run([str(cccc), f"-I{root}/include", cl, c], root)
    return expect("a .cl input does not leak keywords into a later .c input", r.returncode == 42, r)


def case_native(cccc, tmp, root):
    src = write(tmp, "native.cl", KERNEL_SRC)
    exe = str(Path(tmp) / "native_out")
    built = run([str(cccc), f"-I{root}/include", "-c=native", src, "-o", exe], root)
    if built.returncode != 0:
        return expect("-c=native builds a .cl file", False, built)
    ran = run([exe], root)
    return expect("-c=native builds and runs a .cl file", ran.returncode == 42, ran)


def case_foreign_cwd(cccc, tmp, root):
    src = write(tmp, "cwd.cl", KERNEL_SRC)
    r = run([str(cccc), src], tmp)
    return expect("the prelude is found from a directory without include/", r.returncode == 42, r)


def case_native_foreign_cwd(cccc, tmp, root):
    src = write(tmp, "native_cwd.cl", KERNEL_SRC)
    exe = str(Path(tmp) / "native_cwd_out")
    built = run([str(cccc), "-c=native", src, "-o", exe], tmp)
    if built.returncode != 0:
        return expect("-c=native builds a .cl file with no include/", False, built)
    ran = run([exe], tmp)
    return expect("-c=native from a directory without include/", ran.returncode == 42, ran)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", help="Path to the cccc binary (default: ./cccc)")
    args = parser.parse_args(argv)

    root = Path(__file__).parent.parent.resolve()
    cccc = Path(args.binary).resolve() if args.binary else root / "cccc"

    print("OpenCL C dialect selection smoke tests")
    if not cccc.exists():
        print(f"  FAIL: {cccc} not found -- run 'make' first.")
        return 1

    cases = [
        case_cl_extension,
        case_x_cl,
        case_x_c,
        case_c_rejects_keywords,
        case_cl_std,
        case_unknown_language,
        case_no_macro_leak,
        case_native,
        case_foreign_cwd,
        case_native_foreign_cwd,
    ]
    with tempfile.TemporaryDirectory() as tmp:
        results = [case(cccc, tmp, root) for case in cases]

    if all(results):
        print(f"All {len(results)} OpenCL CLI smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} OpenCL CLI smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
