import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReverseNdjsonTest(unittest.TestCase):
    def test_reverse_reader(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "reverse_ndjson_test"
            subprocess.run([
                compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "src"),
                str(ROOT / "test" / "reverse_ndjson_test.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
