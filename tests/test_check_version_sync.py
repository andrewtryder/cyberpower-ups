#!/usr/bin/env python3
"""Regression tests for scripts/check-version-sync.py."""

import json
import os
import shutil
import sys
import tempfile
import unittest

# Add scripts directory to module path
import importlib.util

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
script_path = os.path.join(REPO_ROOT, "scripts", "check-version-sync.py")
spec = importlib.util.spec_from_file_location("check_version_sync", script_path)
check_version_sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_version_sync)


class TestVersionSync(unittest.TestCase):

  def setUp(self):
    self.temp_dir = tempfile.mkdtemp(prefix="ver_sync_test_")

  def tearDown(self):
    shutil.rmtree(self.temp_dir)

  def write_files(self, cmake_ver="0.7.0", txt_ver="0.7.0", manifest_ver="0.7.0"):
    cmake_content = f"""cmake_minimum_required(VERSION 3.16)
project(cyberpower-ups
  VERSION {cmake_ver} # x-release-please-version
  LANGUAGES CXX
)
"""
    with open(os.path.join(self.temp_dir, "CMakeLists.txt"), "w") as f:
      f.write(cmake_content)

    with open(os.path.join(self.temp_dir, "version.txt"), "w") as f:
      f.write(txt_ver + "\n")

    manifest = {"." : manifest_ver}
    with open(os.path.join(self.temp_dir, ".release-please-manifest.json"), "w") as f:
      json.dump(manifest, f)

  def test_exact_match(self):
    self.write_files("0.7.0", "0.7.0", "0.7.0")
    self.assertTrue(check_version_sync.check_version_sync(self.temp_dir))

  def test_bumped_match(self):
    self.write_files("0.7.1", "0.7.1", "0.7.1")
    self.assertTrue(check_version_sync.check_version_sync(self.temp_dir))

  def test_cmake_mismatch(self):
    self.write_files("0.8.0", "0.7.0", "0.7.0")
    self.assertFalse(check_version_sync.check_version_sync(self.temp_dir))

  def test_version_txt_mismatch(self):
    self.write_files("0.7.0", "0.7.1", "0.7.0")
    self.assertFalse(check_version_sync.check_version_sync(self.temp_dir))

  def test_manifest_mismatch(self):
    self.write_files("0.7.0", "0.7.0", "0.7.1")
    self.assertFalse(check_version_sync.check_version_sync(self.temp_dir))

  def test_invalid_semver(self):
    self.write_files("0.7", "0.7", "0.7")
    self.assertFalse(check_version_sync.check_version_sync(self.temp_dir))

  def test_missing_manifest(self):
    self.write_files("0.7.0", "0.7.0", "0.7.0")
    os.remove(os.path.join(self.temp_dir, ".release-please-manifest.json"))
    self.assertFalse(check_version_sync.check_version_sync(self.temp_dir))


if __name__ == "__main__":
  unittest.main()
