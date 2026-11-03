import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bench_cardputer as bench


class AcceptanceTests(unittest.TestCase):
    def samples(self):
        return [dict(target=bench.TARGET, ready=1, running=1, saving=0, sd=1, upload=0,
                     min_internal=40000, largest_internal=20000, loop_stack=2048,
                     writer_stack=2048, ui_samples=i, ui_p95_ms=80, ui_max_ms=200,
                     drops=3, errors=0, gps_fix=1, lora_rx=i) for i in range(1800)]

    def test_complete_window_passes_without_claiming_lossless_reception(self):
        result = bench.evaluate(self.samples(), 0, 1800)
        self.assertEqual(result["result"], "passed")
        self.assertEqual(result["metrics"]["drop_delta"], 0)
        self.assertIn("not RF loss", result["scope"])

    def test_missing_metrics_input_short_window_and_wrong_target_fail(self):
        for field, value in (("target", "heltec-v4-r8-tft"), ("ui_samples", 0),
                             ("min_internal", 1000), ("upload", 1)):
            samples = self.samples()
            for sample in samples:
                sample[field] = value
            self.assertEqual(bench.evaluate(samples, 0, 1800)["result"], "failed")
        self.assertEqual(bench.evaluate(self.samples(), 0, 60)["result"], "failed")
        self.assertEqual(bench.evaluate([], 1, 1800)["result"], "failed")


if __name__ == "__main__":
    unittest.main()
