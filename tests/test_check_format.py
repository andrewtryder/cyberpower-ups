#!/usr/bin/env python3
"""Regression tests for scripts/check-format.sh.

Verifies that:
1. Unmodified code passes cleanly.
2. Correctly formatted changes pass without false positives (even when
   git-clang-format outputs 'clang-format did not modify any files').
3. Malformed changes fail and produce descriptive diffs.
4. Files directly under src/, tools/, tests/, and include/ are properly checked.
"""

import os
import subprocess
import sys


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    os.chdir(repo_root)

    check_format_script = os.path.join(repo_root, "scripts", "check-format.sh")
    if not os.path.exists(check_format_script):
        print(f"ERROR: {check_format_script} not found", file=sys.stderr)
        return 1

    # Verify baseline with no modifications
    baseline = subprocess.run([check_format_script, "HEAD"], capture_output=True, text=True)
    if baseline.returncode != 0:
        print(f"FAIL: Baseline check failed unexpectedly:\n{baseline.stderr}", file=sys.stderr)
        return 1
    print("PASS: Baseline check on unmodified repo")

    targets = [
        "src/hid_mapping.cpp",
        "tools/cpups.cpp",
        "tests/offline_test.cpp",
        "include/cyberpower/protocol_hid.hpp",
    ]

    clean_snippet = (
        "\nnamespace check_format_test {\n"
        "inline int helper(int a, int b) {\n"
        "  return a + b;\n"
        "}\n"
        "}  // namespace check_format_test\n"
    )

    malformed_snippet = "\nvoid bad_func( int  x,int y ){int z=x+y;return;}\n"

    for target in targets:
        target_path = os.path.join(repo_root, target)
        if not os.path.exists(target_path):
            print(f"SKIP: {target} does not exist", file=sys.stderr)
            continue

        with open(target_path, "r", encoding="utf-8") as f:
            orig = f.read()

        try:
            # 1. Test clean, properly formatted addition
            with open(target_path, "w", encoding="utf-8") as f:
                f.write(orig + clean_snippet)

            clean_run = subprocess.run([check_format_script, "HEAD"], capture_output=True, text=True)
            if clean_run.returncode != 0:
                print(
                    f"FAIL: Clean addition in {target} was incorrectly rejected:\n{clean_run.stderr}",
                    file=sys.stderr,
                )
                return 1
            print(f"PASS (clean addition accepted): {target}")

            # 2. Test malformed addition violating formatting rules
            with open(target_path, "w", encoding="utf-8") as f:
                f.write(orig + malformed_snippet)

            bad_run = subprocess.run([check_format_script, "HEAD"], capture_output=True, text=True)
            if bad_run.returncode == 0:
                print(f"FAIL: Malformed code in {target} was not detected!", file=sys.stderr)
                return 1
            if target not in bad_run.stderr and target not in bad_run.stdout:
                print(
                    f"FAIL: Expected {target} to be identified in diff output:\n{bad_run.stderr}",
                    file=sys.stderr,
                )
                return 1
            print(f"PASS (malformed addition rejected): {target}")

        finally:
            with open(target_path, "w", encoding="utf-8") as f:
                f.write(orig)

    print("\nAll formatting gate regression tests passed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

