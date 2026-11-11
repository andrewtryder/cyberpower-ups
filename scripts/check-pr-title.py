#!/usr/bin/env python3
"""Validates that a pull request title adheres to the Conventional Commits specification.

Usage:
  python3 scripts/check-pr-title.py "feat(linux): my title"
  or with environment variable:
  PR_TITLE="feat(linux): my title" python3 scripts/check-pr-title.py
"""

import os
import re
import sys

PATTERN = re.compile(
    r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)"
    r"(\([a-zA-Z0-9_\-\./]+\))?"
    r"(!)?"
    r": (.+)$"
)

VALID_TYPES = (
    "feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert"
)


def validate_title(title: str) -> bool:
  title = title.strip()
  if not title:
    print("ERROR: PR title is empty.", file=sys.stderr)
    return False

  if PATTERN.match(title):
    print(f"OK: Valid Conventional PR title: {title}")
    return True

  print(
      f"ERROR: PR title does not conform to Conventional Commits specification.\n"
      f"Title: '{title}'\n\n"
      f"Expected format:\n"
      f"  <type>[(<scope>)][!]: <description>\n\n"
      f"Valid types:\n"
      f"  {VALID_TYPES}\n\n"
      f"Examples:\n"
      f"  feat: add a new status field\n"
      f"  fix(linux): correct HID report parsing\n"
      f"  ci: introduce automated code quality gates\n"
      f"  docs: document Linux device permissions\n"
      f"  refactor(serial): consolidate POSIX transport logic\n"
      f"  feat!: change a public API contract\n",
      file=sys.stderr,
  )
  return False


def main() -> int:
  if len(sys.argv) > 1:
    title = sys.argv[1]
  else:
    title = os.environ.get("PR_TITLE", "")

  if not validate_title(title):
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())

