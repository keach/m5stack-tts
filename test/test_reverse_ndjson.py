import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReverseNdjsonTest(unittest.TestCase):
    def test_diagnostic_model(self):
        self.compile_and_run("diagnostic_model_test.cpp")

    def test_p2p_connection_status(self):
        self.compile_and_run("p2p_connection_test.cpp")

    def test_reverse_reader(self):
        self.compile_and_run("reverse_ndjson_test.cpp")

    def compile_and_run(self, source):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "reverse_ndjson_test"
            subprocess.run([
                compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "src"),
                str(ROOT / "test" / source),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
