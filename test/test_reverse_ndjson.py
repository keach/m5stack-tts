import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARDUINOJSON_INCLUDE = (
    ROOT / ".pio" / "libdeps" / "m5stack-basic" / "ArduinoJson" / "src"
)


class ReverseNdjsonTest(unittest.TestCase):
    def test_diagnostic_model(self):
        self.compile_and_run("diagnostic_model_test.cpp")

    def test_p2p_connection_status(self):
        self.compile_and_run("p2p_connection_test.cpp")

    def test_p2p_json_buffer(self):
        self.assertTrue(
            ARDUINOJSON_INCLUDE.is_dir(),
            "ArduinoJson is required; run `.venv/bin/pio run -e m5stack-basic` first",
        )
        self.compile_and_run(
            "p2p_json_buffer_test.cpp", include_dirs=(ARDUINOJSON_INCLUDE,)
        )

    def test_reverse_reader(self):
        self.compile_and_run("reverse_ndjson_test.cpp")

    def compile_and_run(self, source, include_dirs=()):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "reverse_ndjson_test"
            command = [
                compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "src"),
            ]
            for include_dir in include_dirs:
                command.extend(["-I", str(include_dir)])
            command.extend([str(ROOT / "test" / source), "-o", str(binary)])
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, cwd=ROOT)
