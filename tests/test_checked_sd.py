from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CheckedStorageTests(unittest.TestCase):
    def test_actual_helper_and_logger_report_deferred_io_failures(self):
        result = subprocess.run([sys.executable, "tools/check_checked_sd.py"], cwd=ROOT,
                                check=True, capture_output=True, text=True)
        self.assertIn("checks passed (Cardputer)", result.stdout)
        self.assertIn("checks passed (Heltec)", result.stdout)


if __name__ == "__main__":
    unittest.main()
