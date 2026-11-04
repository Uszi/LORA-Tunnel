"""Controlled serial/clock fakes only; these tests never open a serial port."""
import importlib.util
from pathlib import Path
import unittest

MODULE = Path(__file__).resolve().parents[1] / "tools" / "bench_serial.py"
spec = importlib.util.spec_from_file_location("bench_serial", MODULE)
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def status(**changes):
    values = dict(version="0.1.1-dev", uptime_ms=1000, ready=1, running=0, saving=0,
                  sd=1, touch=1, lora=1, gps_chars=123, gps_fix=0, sats=0,
                  wifi=0, ble=0, lora_rx=0, drops=0, errors=0, free_heap=100000)
    values.update(changes)
    return ("LT STATUS " + " ".join(f"{key}={value}" for key, value in values.items()) + "\n").encode()


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeSerial:
    def __init__(self, clock, responder, initial=b""):
        self.clock, self.responder = clock, responder
        self.pending = bytearray(initial)
        self.writes = []
        self.reset_count = self.max_read_requested = 0

    def reset_input_buffer(self):
        self.pending.clear()
        self.reset_count += 1

    def write(self, command):
        self.writes.append(command)
        self.pending.extend(self.responder(command, len(self.writes)))
        return len(command)

    def read(self, size):
        self.max_read_requested = max(size, self.max_read_requested)
        self.clock.sleep(0.2)
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data


class BenchSerialTests(unittest.TestCase):
    def exercise(self, command="status", seconds=3, responder=lambda command, count: status(), initial=b"", **options):
        clock = FakeClock()
        connection = FakeSerial(clock, responder, initial)
        output = []
        result = bench.run_bench(connection, command, seconds, clock=clock.monotonic,
                                 sleep=clock.sleep, emit=output.append, **options)
        return result, connection, "\n".join(output)

    def test_optional_cardputer_metrics_are_allowlisted_and_validated(self):
        fields=dict(target="cardputer-adv-lora1262",min_internal=40000,largest_internal=20000,
                    loop_stack=2048,writer_stack=2048,ui_p95_ms=40,ui_max_ms=80,upload=0,
                    input_ok=1,cap_ok=1,init_writer=1,init_scanner=1,init_memory=1,
                    scanner_stage="ble_init",heap_hw=150000,heap_ui=110000,
                    heap_config=108000,heap_before_scan=80000)
        result,_,output=self.exercise(responder=lambda *_: status(**fields,untrusted="private"))
        self.assertEqual(result,0)
        self.assertIn("min_internal=40000",output)
        self.assertIn("target=cardputer-adv-lora1262",output)
        self.assertNotIn("private",output)
        self.assertIsNone(bench.parse_status(status(**dict(fields,ui_max_ms=-1)).decode().strip()))
        self.assertIsNone(bench.parse_status(status(**dict(fields,upload=2)).decode().strip()))
        self.assertIn("scanner_stage=ble_init",output)
        self.assertIsNone(bench.parse_status(status(**dict(fields,input_ok=2)).decode().strip()))
        self.assertIsNone(bench.parse_status(status(**dict(fields,scanner_stage="invalid/value")).decode().strip()))
        self.assertIsNone(bench.parse_status(status(**dict(fields,heap_hw=-1)).decode().strip()))

    def test_upload_resource_metrics_are_nonsecret_optional_integers(self):
        fields=dict(upload_runs=17,upload_min_free=42000,upload_largest=24000,upload_stack=4096,upload_check=2)
        parsed=bench.parse_status(status(**fields,api_token="private").decode().strip())
        for name,value in fields.items():
            self.assertEqual(parsed[name],value)
            self.assertIsNone(bench.parse_status(status(**dict(fields,**{name:-1})).decode().strip()))
        self.assertNotIn("api_token",parsed)

    def test_silent_device_fails_even_with_stale_buffered_status(self):
        result, connection, output = self.exercise(responder=lambda *_: b"", initial=status())
        self.assertNotEqual(result, 0)
        self.assertEqual(connection.reset_count, 1)
        self.assertIn("no valid post-command", output)
        self.assertNotIn("LT STATUS version=", output)
        self.assertGreater(len(connection.writes), 1)

    def test_refused_start_fails_and_start_is_not_repeated(self):
        result, connection, output = self.exercise(command="start", responder=lambda *_: status(ready=0))
        self.assertNotEqual(result, 0)
        self.assertEqual(connection.writes.count(b"tunnel start\n"), 1)
        self.assertTrue(all(line == b"tunnel status\n" for line in connection.writes[1:]))
        self.assertIn("did not reach running=1", output)

    def test_stop_waits_until_saving_drains_without_repeating_stop(self):
        result, connection, output = self.exercise(
            command="stop", seconds=4,
            responder=lambda _, count: status(saving=1 if count < 3 else 0,
                                               uptime_ms=1000 + count * 1000))
        self.assertEqual(result, 0)
        self.assertEqual(connection.writes.count(b"tunnel stop\n"), 1)
        self.assertIn("saving=1", output)
        self.assertIn("saving=0", output)
        self.assertIn("Bench PASS", output)

    def test_stop_still_saving_at_deadline_fails(self):
        result, _, output = self.exercise(command="stop", responder=lambda *_: status(saving=1))
        self.assertNotEqual(result, 0)
        self.assertIn("did not reach running=0 saving=0", output)

    def test_complete_start_status_succeeds(self):
        result, connection, _ = self.exercise(command="start", responder=lambda *_: status(running=1))
        self.assertEqual(result, 0)
        self.assertEqual(connection.writes.count(b"tunnel start\n"), 1)

    def test_missing_fields_invalid_numbers_and_duplicates_do_not_ack(self):
        invalid = [b"LT STATUS running=1\n", status(ready=2), status(free_heap="NaN"),
                   status()[:-1] + b" running=0\n"]
        for response in invalid:
            with self.subTest(response=response):
                result, _, output = self.exercise(responder=lambda *_, response=response: response)
                self.assertNotEqual(result, 0)
                self.assertIn("no valid post-command", output)

    def test_status_then_silence_fails_freshness(self):
        result, _, output = self.exercise(seconds=5, responder=lambda _, count: status() if count == 1 else b"")
        self.assertNotEqual(result, 0)
        self.assertIn("status polling timed out", output)

    def test_device_diagnostics_fail_even_after_a_valid_status(self):
        for diagnostic in (b"Guru Meditation Error: panic", b"Brownout detector was triggered",
                           b"assert failed: private_memory", b"Rebooting...", b"ESP-ROM:esp32s3",
                           b"rst:0x1 (POWERON)"):
            with self.subTest(diagnostic=diagnostic):
                result, _, output = self.exercise(
                    responder=lambda _, count, diagnostic=diagnostic: status() if count == 1 else diagnostic + b"\n")
                self.assertNotEqual(result, 0)
                self.assertIn("diagnostic after command", output)
                self.assertNotIn("private_memory", output)

    def test_oversize_fragment_is_discarded_until_newline_and_privacy_preserved(self):
        raw = b"LT STATUS token=PRIVATE_SECRET " + b"x" * 6000 + b"\n"
        raw += b"SSID=PRIVATE_NETWORK\n" + status()[:-1] + b" secret=PRIVATE_SECRET\n"
        result, connection, output = self.exercise(seconds=4, responder=lambda _, count: raw if count == 1 else status())
        self.assertEqual(result, 0)
        self.assertEqual(connection.max_read_requested, 1024)
        self.assertIn("oversize_lines=1", output)
        self.assertNotIn("PRIVATE_SECRET", output)
        self.assertNotIn("PRIVATE_NETWORK", output)

    def test_uptime_decrease_fails_without_claiming_success(self):
        result, _, output = self.exercise(responder=lambda _, count: status(uptime_ms=10000 if count == 1 else 20))
        self.assertNotEqual(result, 0)
        self.assertIn("uptime decreased", output)


    def test_watchdog_trigger_fails_but_benign_watchdog_text_does_not(self):
        for diagnostic in (b"E (13) task_wdt: Task watchdog got triggered.",
                           b"E (13) task_wdt: Aborting.", b"Interrupt wdt timeout on CPU0"):
            result, _, output = self.exercise(responder=lambda *_, d=diagnostic: d + b"\n" + status())
            self.assertNotEqual(result, 0)
            self.assertIn("device watchdog diagnostic", output)
        result, _, _ = self.exercise(responder=lambda *_: b"watchdog enabled; task_wdt: initialized\n" + status())
        self.assertEqual(result, 0)

    def test_strict_intermediate_gap_fails_even_if_device_would_recover(self):
        observation = {}
        result, connection, _ = self.exercise(seconds=15, strict=True, observation=observation,
            responder=lambda _, n: b"" if 2 <= n < 9 else status(uptime_ms=n * 1000))
        self.assertNotEqual(result, 0)
        self.assertLess(len(connection.writes), 9)
        self.assertGreater(observation["max_status_gap_seconds"], bench.STATUS_MAX_AGE_SECONDS)

    def test_strict_malformed_oversize_and_partial_required_status_fail(self):
        for raw in (b"LT STATUS ready=broken\n", b"LT STATUS secret=" + b"x" * 5000 + b"\n",
                    b"LT STATUS version=0.4.0"):
            result, _, output = self.exercise(seconds=3, strict=True,
                responder=lambda _, n, raw=raw: status(uptime_ms=n * 1000) if n == 1 else raw)
            self.assertNotEqual(result, 0)
            self.assertNotIn("secret=", output)
        # Required public fields must be valid even when the rest is complete.
        result, _, _ = self.exercise(strict=True, responder=lambda *_: status(mode="unknown"))
        self.assertNotEqual(result, 0)

    def test_strict_constant_uptime_fails_and_valid_timestamped_callbacks_pass(self):
        result, _, _ = self.exercise(strict=True)
        self.assertNotEqual(result, 0)
        samples, observation = [], {}
        result, _, output = self.exercise(strict=True, observation=observation,
            on_status=lambda sample, elapsed: samples.append((sample, elapsed)),
            responder=lambda _, n: status(uptime_ms=n * 1000, mode="wardriver", matches=n, secret="PRIVATE"))
        self.assertEqual(result, 0)
        self.assertEqual(observation["valid_status"], len(samples))
        self.assertGreaterEqual(observation["elapsed_seconds"], 3)
        self.assertNotIn("LT STATUS", output)
        self.assertNotIn("PRIVATE", str(samples))
        self.assertEqual(samples[-1][0]["mode"], "wardriver")
        self.assertTrue(all(b[1] > a[1] for a, b in zip(samples, samples[1:])))

    def test_disconnect_short_write_and_interrupt_return_failure(self):
        class BrokenSerial(FakeSerial):
            def read(self, size):
                raise OSError("private USB details")
        class ShortSerial(FakeSerial):
            def write(self, command):
                return len(command) - 1
        class InterruptedSerial(FakeSerial):
            def read(self, size):
                raise KeyboardInterrupt
        for kind in (BrokenSerial, ShortSerial, InterruptedSerial):
            clock, output, observation = FakeClock(), [], {}
            connection = kind(clock, lambda *_: status())
            result = bench.run_bench(connection, "status", 3, strict=True,
                clock=clock.monotonic, sleep=clock.sleep, emit=output.append, observation=observation)
            self.assertNotEqual(result, 0)
            self.assertEqual(observation["result"], result)
            self.assertNotIn("private USB", str(output))

    def test_strict_status_flood_is_bounded(self):
        result, _, output = self.exercise(seconds=3, strict=True,
            responder=lambda *_: b"".join(status(uptime_ms=n) for n in range(1, 30)))
        self.assertNotEqual(result, 0)
        self.assertIn("bounded observation limit", output)


if __name__ == "__main__":
    unittest.main()
