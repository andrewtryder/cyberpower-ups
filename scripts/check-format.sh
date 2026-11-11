#!/usr/bin/env bash
# Checks that changed C/C++ lines conform to .clang-format.
# Usage:
#   scripts/check-format.sh [base_ref]
# Example:
#   scripts/check-format.sh origin/main

set -euo pipefail

BASE_REF="${1:-origin/main}"

if ! command -v git-clang-format >/dev/null 2>&1; then
  echo "ERROR: git-clang-format is not installed or not in PATH." >&2
  exit 1
fi

echo "Checking C/C++ code formatting against ${BASE_REF}..."

# Run git-clang-format in diff mode.
DIFF_OUTPUT="$(git-clang-format --diff "${BASE_REF}" -- 'include/**/*.hpp' 'include/**/*.h' 'src/**/*.cpp' 'src/**/*.hpp' 'tools/**/*.cpp' 'examples/**/*.cpp' 'tests/**/*.cpp')"

if [ -n "${DIFF_OUTPUT}" ] && [ "${DIFF_OUTPUT}" != "no modified files to format" ]; then
  echo "ERROR: Formatting violations detected against ${BASE_REF}!" >&2
  echo "" >&2
  echo "${DIFF_OUTPUT}" >&2
  echo "" >&2
  echo "To automatically format your changed lines, run:" >&2
  echo "  git-clang-format ${BASE_REF}" >&2
  exit 1
fi

echo "OK: No formatting violations found in changed files."

