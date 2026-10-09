#!/usr/bin/env python3
"""Compile and run the SD font page-chunk layout test on the host."""

import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_FILE = REPO_ROOT / "test" / "ChunkedBitmapLayoutTest.cpp"
INCLUDE = REPO_ROOT / "lib" / "EpdFont"


def find_compiler() -> list[str]:
    if compiler := os.environ.get("CXX"):
        return shlex.split(compiler)
    for compiler in ("g++", "clang++", "c++"):
        if executable := shutil.which(compiler):
            return [executable]
    raise RuntimeError("No host C++ compiler found; set CXX or install g++ or clang++")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="chunked-bitmap-layout-") as temp_dir:
        binary = Path(temp_dir) / "ChunkedBitmapLayoutTest"
        command = find_compiler() + ["-std=c++20", "-Wall", "-Wextra", "-Werror", f"-I{INCLUDE}", str(TEST_FILE),
                                     "-o", str(binary)]
        result = subprocess.run(command, text=True, capture_output=True, cwd=REPO_ROOT)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            return result.returncode
        run = subprocess.run([str(binary)], text=True, capture_output=True, cwd=REPO_ROOT)
        sys.stdout.write(run.stdout)
        sys.stderr.write(run.stderr)
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
