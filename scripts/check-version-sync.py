#!/usr/bin/env python3
"""Validates that project versions are strictly synchronized across:
  1. CMakeLists.txt (project VERSION)
  2. version.txt (root version file)
  3. .release-please-manifest.json (Release Please manifest)

Exits with 0 if all versions match and conform to SemVer; exits with 1 otherwise.
"""

import json
import os
import re
import sys

SEMVER_REGEX = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$"
)

CMAKE_VERSION_REGEX = re.compile(
    r"project\s*\(\s*cyberpower-ups[\s\S]*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+(?:-[\w.]+)?(?:\+[-\w.]+)?)"
)


def get_repo_root() -> str:
  script_dir = os.path.dirname(os.path.abspath(__file__))
  return os.path.dirname(script_dir)


def check_version_sync(root_dir: str = None) -> bool:
  if root_dir is None:
    root_dir = get_repo_root()

  cmake_path = os.path.join(root_dir, "CMakeLists.txt")
  version_txt_path = os.path.join(root_dir, "version.txt")
  manifest_path = os.path.join(root_dir, ".release-please-manifest.json")

  errors = []

  # 1. Read CMakeLists.txt
  cmake_version = None
  if not os.path.isfile(cmake_path):
    errors.append(f"Missing file: {cmake_path}")
  else:
    with open(cmake_path, "r", encoding="utf-8") as f:
      content = f.read()
    match = CMAKE_VERSION_REGEX.search(content)
    if not match:
      errors.append("Failed to locate 'VERSION <semver>' in CMakeLists.txt")
    else:
      cmake_version = match.group(1).strip()

  # 2. Read version.txt
  txt_version = None
  if not os.path.isfile(version_txt_path):
    errors.append(f"Missing file: {version_txt_path}")
  else:
    with open(version_txt_path, "r", encoding="utf-8") as f:
      txt_version = f.read().strip()

  # 3. Read .release-please-manifest.json
  manifest_version = None
  if not os.path.isfile(manifest_path):
    errors.append(f"Missing file: {manifest_path}")
  else:
    try:
      with open(manifest_path, "r", encoding="utf-8") as f:
        data = json.load(f)
      manifest_version = data.get(".")
      if not manifest_version:
        errors.append(f"No '.' key found in manifest: {manifest_path}")
    except Exception as e:
      errors.append(f"Failed to parse manifest {manifest_path}: {e}")

  if errors:
    print("ERROR: Version synchronization check failed:", file=sys.stderr)
    for err in errors:
      print(f"  - {err}", file=sys.stderr)
    return False

  # Validate SemVer format
  for name, ver in [
      ("CMakeLists.txt", cmake_version),
      ("version.txt", txt_version),
      (".release-please-manifest.json", manifest_version),
  ]:
    if not SEMVER_REGEX.match(ver):
      print(
          f"ERROR: {name} contains invalid SemVer string '{ver}'",
          file=sys.stderr,
      )
      return False

  # Validate exact match
  if not (cmake_version == txt_version == manifest_version):
    print("ERROR: Version mismatch across configuration files:", file=sys.stderr)
    print(f"  - CMakeLists.txt:               {cmake_version}", file=sys.stderr)
    print(f"  - version.txt:                  {txt_version}", file=sys.stderr)
    print(f"  - .release-please-manifest.json: {manifest_version}", file=sys.stderr)
    return False

  print(
      f"OK: Version synchronization verified: {cmake_version} "
      f"(CMakeLists.txt == version.txt == .release-please-manifest.json)"
  )
  return True


def main() -> int:
  root = sys.argv[1] if len(sys.argv) > 1 else None
  if not check_version_sync(root):
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
