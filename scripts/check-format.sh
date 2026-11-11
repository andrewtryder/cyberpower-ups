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

# Source directories to check. Passing directories directly ensures files
# directly within them (e.g. src/hid_mapping.cpp, tools/cpups.cpp, tests/offline_test.cpp)
# as well as nested files are covered.
DIRS=()
for d in include src tools examples tests; do
  if [ -d "$d" ]; then
    DIRS+=("$d")
  fi
done

if [ ${#DIRS[@]} -eq 0 ]; then
  echo "OK: No source directories found to check."
  exit 0
fi

# Run git-clang-format with --quiet and --diff.
# --quiet suppresses informational messages ("no modified files to format", "clang-format did not modify any files")
# while outputting only genuine diff content if violations exist.
# We also filter out any lingering informational lines for safety across git-clang-format versions.
set +e
RAW_OUTPUT="$(git-clang-format --quiet --diff "${BASE_REF}" --extensions c,h,cpp,hpp -- "${DIRS[@]}" 2>&1)"
FORMAT_STATUS=$?
set -e

# Strip known informational notices if emitted by some git-clang-format implementations
DIFF_OUTPUT="$(echo "${RAW_OUTPUT}" | grep -v -E '^(no modified files to format|clang-format did not modify any files)$' || true)"

if [ ${FORMAT_STATUS} -ne 0 ] || [ -n "${DIFF_OUTPUT}" ]; then
  echo "ERROR: Formatting violations detected against ${BASE_REF}!" >&2
  echo "" >&2
  if [ -n "${DIFF_OUTPUT}" ]; then
    echo "${DIFF_OUTPUT}" >&2
    echo "" >&2
  fi
  echo "To automatically format your changed lines, run:" >&2
  echo "  git-clang-format ${BASE_REF} -- ${DIRS[*]}" >&2
  exit 1
fi

echo "OK: No formatting violations found in changed files."
