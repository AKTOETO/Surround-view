"""Exercise the installed-style native CLI before any report or GPU work."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


BINARY = str(Path(sys.argv.pop(1)).resolve())
CONFIG = str(Path(sys.argv.pop(1)).resolve())


class PlatformCliTests(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run(
            [BINARY, *arguments], capture_output=True, text=True, timeout=10
        )

    def test_help_needs_no_config_or_gpu(self):
        for flag in ("--help", "-h"):
            with self.subTest(flag=flag):
                result = self.invoke(flag)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Usage: sv-platform-test", result.stdout)
                self.assertIn("--opencv-threads", result.stdout)
                self.assertEqual(result.stderr, "")

    def test_invalid_numbers_never_create_report_directory(self):
        with tempfile.TemporaryDirectory(prefix="sv-platform-cli-") as directory:
            output = Path(directory) / "report"
            for flag, low, high in (
                ("--iterations", 2, 10000),
                ("--warmup", 0, 1000),
                ("--repeats", 1, 10),
                ("--opencv-threads", 1, 64),
            ):
                for value in ("", "2junk", "2.5", "2e1", " 2", "+2",
                              "999999999999999999999", str(low - 1), str(high + 1)):
                    with self.subTest(flag=flag, value=value):
                        result = self.invoke(
                            "--config", CONFIG, "--output", str(output), "--cpu-only",
                            flag, value,
                        )
                        self.assertEqual(result.returncode, 1, result.stdout)
                        self.assertIn(flag + " requires an integer", result.stderr)
                        self.assertFalse(output.exists())

    def test_missing_and_unknown_options_name_the_error(self):
        result = self.invoke("--iterations")
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing argument value: --iterations", result.stderr)
        result = self.invoke("--unknown")
        self.assertEqual(result.returncode, 1)
        self.assertIn("unknown argument: --unknown", result.stderr)


if __name__ == "__main__":
    unittest.main()
