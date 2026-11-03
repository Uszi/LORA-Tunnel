import argparse
import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bench_cardputer as bench
from bench_serial import run_bench
from test_bench_serial import FakeClock, FakeSerial, status


class AcceptanceTests(unittest.TestCase):
    def samples(self):
        return [dict(version="0.4.0-beta.1", target=bench.TARGET, ready=1, running=1,
                     saving=0, sd=1, upload=0, touch=0, lora=1, gps_chars=i, sats=4,
                     wifi=i, ble=i, free_heap=41000, uptime_ms=1000+i*1000,
                     min_internal=40000, largest_internal=20000, loop_stack=2048,
                     writer_stack=2048, ui_samples=i, ui_p95_ms=80, ui_max_ms=200,
                     input_ok=1, cap_ok=1, init_writer=1, init_scanner=1, init_memory=1,
                     mode="wardriver", matches=i, drops=3, errors=0, gps_fix=1,
                     lora_rx=i, elapsed_seconds=i+0.2) for i in range(1800)]

    def observation(self):
        return dict(result=0, strict=True, elapsed_seconds=1800, valid_status=1800,
                    malformed_status=0, oversize_lines=0, other_lines_omitted=0,
                    max_status_gap_seconds=1, first_status_seconds=0.2, last_status_seconds=1799.2)

    def evaluate(self, samples=None, **kwargs):
        return bench.evaluate(self.samples() if samples is None else samples, 0, 1800,
                              observation=self.observation(), **kwargs)

    def test_complete_window_passes_without_claiming_lossless_reception(self):
        result = self.evaluate()
        self.assertEqual(result["result"], "passed")
        self.assertEqual(result["metrics"]["drop_delta"], 0)
        self.assertEqual(len(result["samples"]), 1800)
        self.assertIn("not RF loss", result["scope"])
        self.assertIn("last 64", result["measurement_scopes"]["ui"])
        self.assertIn("lifetime", result["measurement_scopes"]["min_internal"])

    def test_missing_metrics_readiness_short_window_and_wrong_target_fail(self):
        for field, value in (("target", "heltec-v4-r8-tft"), ("ui_samples", 0),
                             ("min_internal", 1000), ("upload", 1), ("input_ok", 0),
                             ("cap_ok", 0), ("init_writer", 0), ("init_scanner", 0),
                             ("init_memory", 0), ("mode", "recon"), ("loop_stack", 1023),
                             ("writer_stack", 1023), ("largest_internal", 16383)):
            with self.subTest(field=field):
                samples = self.samples()
                for sample in samples:
                    sample[field] = value
                self.assertEqual(self.evaluate(samples)["result"], "failed")
        samples = self.samples()
        del samples[300]["drops"]
        self.assertEqual(self.evaluate(samples)["result"], "failed")
        self.assertEqual(bench.evaluate(self.samples(), 0, 60, observation=self.observation())["result"], "failed")
        self.assertEqual(bench.evaluate([], 1, 1800)["result"], "failed")
        self.assertEqual(bench.evaluate(self.samples(), 0, 1800)["result"], "failed")

    def test_error_spike_reset_and_version_change_fail(self):
        for field, value in (("errors", 1), ("drops", 2), ("ui_samples", 0),
                             ("uptime_ms", 1), ("wifi", 0), ("version", "0.3.0")):
            samples = self.samples()
            samples[400][field] = value
            self.assertEqual(self.evaluate(samples)["result"], "failed", field)
        samples = self.samples()
        for sample in samples:
            sample["errors"] = 4
        report = self.evaluate(samples)
        self.assertEqual(report["result"], "passed")
        self.assertEqual(report["metrics"]["error_initial"], 4)
        self.assertEqual(report["metrics"]["error_delta"], 0)

    def test_requested_duration_and_count_cannot_hide_gaps_or_missing_observation(self):
        for key, value in (("elapsed_seconds", 60), ("strict", False), ("malformed_status", 1),
                           ("valid_status", 1799), ("max_status_gap_seconds", 4), ("result", 1)):
            observation = self.observation()
            observation[key] = value
            report = bench.evaluate(self.samples(), 0, 1800, observation=observation)
            self.assertEqual(report["result"], "failed", key)
        for change in ("missing", "duplicate", "late", "early_end"):
            samples = self.samples()
            if change == "missing": del samples[300:400]
            elif change == "duplicate": samples[300]["elapsed_seconds"] = samples[299]["elapsed_seconds"]
            elif change == "late": samples[0]["elapsed_seconds"] = 4
            else: samples = samples[:-100]
            self.assertEqual(self.evaluate(samples)["result"], "failed", change)

    def test_hash_binding_and_samples_never_claim_device_verification_or_keep_unknown_data(self):
        samples = self.samples()
        samples[0]["api_token"] = "PRIVATE"
        samples[0]["raw_line"] = "PRIVATE"
        observation = self.observation()
        observation["reason"] = "PRIVATE"
        result = bench.evaluate(samples, 0, 1800, observation=observation, expected_app_sha256="A"*64)
        self.assertEqual(result["identity"]["operator_expected_app_sha256"], "a"*64)
        self.assertFalse(result["identity"]["device_app_sha256_verified"])
        self.assertNotIn("PRIVATE", json.dumps(result))
        for value in ("", "a"*63, "g"*64, "a"*64+" private"):
            with self.assertRaises(argparse.ArgumentTypeError): bench.app_sha256(value)

    def test_lifetime_low_water_remains_strict_and_recon_is_explicit(self):
        samples = self.samples()
        for sample in samples:
            sample["min_internal"] = 17000
        report = self.evaluate(samples)
        self.assertEqual(report["result"], "failed")
        self.assertEqual(report["metrics"]["free_heap_sampled_min"], 41000)
        samples = self.samples()
        for sample in samples: sample["mode"] = "recon"
        self.assertEqual(self.evaluate(samples, expected_mode="recon")["result"], "passed")

    def test_evidence_bound_and_counter_baseline_are_explicit(self):
        samples = self.samples() * 5
        report = self.evaluate(samples)
        self.assertEqual(report["result"], "failed")
        self.assertEqual(len(report["samples"]), bench.MAX_SAMPLES)
        self.assertEqual(report["metrics"]["sample_count"], 9000)
        report = self.evaluate()
        self.assertEqual(report["metrics"]["counter_initial"]["drops"], 3)
        self.assertEqual(report["metrics"]["counter_final"]["drops"], 3)

    def test_actual_runner_rejects_previous_false_pass_scenarios(self):
        template = self.samples()[0]
        fields = {k: v for k, v in template.items() if k != "elapsed_seconds"}
        def good(n, **updates):
            return status(**dict(fields, uptime_ms=n*1000, ui_samples=n, **updates))
        cases = {
            "missing": lambda n: b"" if 300 <= n < 400 else good(n),
            "malformed": lambda n: b"LT STATUS ready=broken\n" if 300 <= n < 400 else good(n),
            "watchdog": lambda n: good(n) + (b"E (300000) task_wdt: Task watchdog got triggered.\n" if n == 300 else b""),
            "errors": lambda n: good(n, errors=1 if n == 300 else 0),
        }
        for name, responder in cases.items():
            clock, observation, samples = FakeClock(), {}, []
            connection = FakeSerial(clock, lambda _, n: responder(n))
            result = run_bench(connection, "status", 1800, strict=True, clock=clock.monotonic,
                sleep=clock.sleep, emit=lambda _: None, observation=observation,
                on_status=lambda sample, elapsed: samples.append(dict(sample, elapsed_seconds=elapsed)))
            self.assertEqual(bench.evaluate(samples, result, 1800, observation=observation)["result"], "failed", name)


if __name__ == "__main__":
    unittest.main()
