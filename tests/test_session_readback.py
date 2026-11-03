import contextlib
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_session_readback as readback


TARGET = "cardputer-adv-lora1262"
VERSION = "0.4.0-beta.1"


def record(kind, seq, mode="wardriver"):
    item = dict(schema=1, type=kind, capture_mode=mode, session="1234abcd",
                seq=seq, monotonic_ms=seq * 100, utc=None, utc_age_ms=None,
                gps=dict(valid=False, age_ms=None, lat=None, lon=None,
                         alt_m=None, hdop=None, sats=0))
    if kind in ("session_start", "session_end"):
        item.update(firmware=VERSION, board=readback.TARGET_BOARDS[TARGET],
                    counts=dict.fromkeys((*readback.COUNTER_NAMES, "flush_max_ms"), 0))
    elif kind == "wifi":
        item["wifi"] = dict(bssid="02:00:00:00:00:01", ssid="owned-test",
                            rssi=-40, channel=1, frequency_mhz=2412,
                            security="[ESS]", timestamp_basis="scan_result_delivery")
    elif kind == "ble":
        item["ble"] = dict(address="02:00:00:00:00:02", address_type="random",
                           name="owned-test", rssi=-50, payload_hex="0102",
                           payload_truncated=False, original_len=2,
                           captured_len=2, advertising_len=2)
    elif kind == "lora":
        item["lora"] = dict(frequency_mhz=906.875, bandwidth_khz=250,
                            spreading_factor=11, coding_rate_denominator=5,
                            sync_word=43, preamble_symbols=16, profile="US LongFast",
                            lna=False, rssi=-90, snr=3.5, length=2,
                            original_len=2, truncated=False, raw_hex="0102",
                            crc_status="not_reported", payload_status="not_decoded",
                            classification="unknown")
    return item


def session(mode="wardriver"):
    return [record(kind, index, mode) for index, kind in enumerate(
        ("session_start", "wifi", "ble", "lora", "session_end"), 1)]


class SessionReadbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.validator = readback.load_validator()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "private-session.jsonl"

    def write(self, items):
        self.path.write_text("".join(json.dumps(item) + "\n" for item in items),
                             encoding="utf-8", newline="\n")

    def check(self, items, **kwargs):
        self.write(items)
        return readback.validate_session(self.path, validator=self.validator, **kwargs)

    def codes(self, report):
        return {error["code"] for error in report["errors"]}

    def test_closed_session_counts_identity_and_optional_receive_gates(self):
        items = session()
        items[1]["gps"].update(valid=True, age_ms=10000, lat=0.0, lon=0.0)
        items[1].update(utc="2026-11-03T12:00:00Z", utc_age_ms=1500)
        items[-1]["counts"]["ingress_drops"] = 2
        report = self.check(items, expected_target=TARGET, expected_version=VERSION,
                            min_lora_records=1, min_fresh_gps_records=1)
        self.assertEqual(report["result"], "passed")
        self.assertEqual(report["counts"]["records"], 5)
        self.assertEqual(report["counts"]["fresh_gps_utc_radio_records"], 1)
        self.assertEqual(report["counter_deltas"]["ingress_drops"], 2)

    def test_production_identity_mapping_matches_board_header(self):
        header = (readback.ROOT / "include/board_config.h").read_text()
        pairs = re.findall(r'TARGET_ID\[\] = "([^"]+)";\s*inline constexpr char NAME\[\] = "([^"]+)";', header)
        self.assertEqual(dict(pairs), readback.TARGET_BOARDS)

    def test_missing_first_last_extra_marker_and_record_after_end_fail(self):
        for mutate, code in (
            (lambda x: x[1:], "missing_initial_session_start"),
            (lambda x: x[:-1], "missing_final_session_end"),
            (lambda x: x[:2] + [record("session_start", 3)] + x[3:], "unexpected_session_start"),
            (lambda x: x + [record("wifi", 6)], "record_after_session_end"),
        ):
            with self.subTest(code=code):
                report = self.check(mutate(session()))
                self.assertIn(code, self.codes(report))
                self.assertEqual(report["result"], "failed")

    def test_identity_mode_sequence_and_synthetic_changes_fail(self):
        for field, value, code in (
            ("session", "87654321", "session_mismatch"),
            ("capture_mode", "recon", "mode_mismatch"),
            ("seq", 9, "noncontiguous_sequence"),
            ("synthetic", True, "synthetic_record"),
        ):
            with self.subTest(field=field):
                items = session()
                items[2][field] = value
                self.assertIn(code, self.codes(self.check(items)))
        items = session()
        items[-1]["firmware"] = "unexpected"
        items[-1]["board"] = "unexpected"
        report = self.check(items, expected_target=TARGET, expected_version=VERSION)
        self.assertTrue({"firmware_mismatch", "board_mismatch", "unexpected_firmware",
                         "unexpected_board"}.issubset(self.codes(report)))
        items = session()
        items[2]["target"] = "unexpected"
        self.assertIn("target_mismatch", self.codes(self.check(items, expected_target=TARGET)))
        items = session()
        items[0]["target"] = "heltec-v4-r8-tft"
        self.assertIn("target_mismatch", self.codes(self.check(items)))

    def test_legacy_wardrive_without_optional_extensions_remains_valid(self):
        items = session()
        for item in items:
            item.pop("capture_mode")
            item.pop("utc_age_ms")
        for name in ("original_len", "captured_len", "advertising_len"):
            items[2]["ble"].pop(name)
        for name in ("original_len", "truncated"):
            items[3]["lora"].pop(name)
        self.assertEqual(self.check(items)["result"], "passed")

    def test_fresh_fix_schema_allows_no_fix_age_and_optional_utc(self):
        items = session()
        items[1]["gps"].update(age_ms=50000, hdop=2.0, sats=7)
        self.assertEqual(self.check(items)["result"], "passed")
        items[1]["gps"].update(valid=True, age_ms=10000, lat=0, lon=0)
        self.assertEqual(self.check(items, min_fresh_gps_records=1)["result"], "passed")
        items[1]["gps"]["age_ms"] = 10001
        self.assertIn("schema_validation", self.codes(self.check(items)))
        items = session()
        items[1]["gps"]["lat"] = 0
        self.assertIn("schema_validation", self.codes(self.check(items)))

    def test_invalid_dates_are_checked_without_optional_format_dependencies(self):
        items = session()
        items[1]["utc"] = "2026-02-31T12:00:00Z"
        self.assertIn("schema_validation", self.codes(self.check(items)))

    def test_raw_hex_lengths_original_lengths_and_ble_boundaries(self):
        for kind, index, field, value, code in (
            ("ble", 2, "captured_len", 1, "raw_length_mismatch"),
            ("ble", 2, "original_len", 1, "original_length_below_retained"),
            ("ble", 2, "original_len", 3, "unreported_truncation"),
            ("ble", 2, "advertising_len", 3, "advertising_length_exceeds_retained"),
            ("lora", 3, "length", 1, "raw_length_mismatch"),
            ("lora", 3, "raw_hex", "xyz", "schema_validation"),
        ):
            with self.subTest(kind=kind, field=field):
                items = session()
                items[index][kind][field] = value
                self.assertIn(code, self.codes(self.check(items)))
        items = session("recon")
        wifi = items[1]["wifi"]
        wifi["tx_address"] = wifi.pop("bssid")
        wifi.update(wifi_frame=True, timestamp_basis="packet_callback",
                    raw_frame_hex="0102", captured_len=2, original_len=3,
                    frame_truncated=True, fcs_present=False)
        self.assertEqual(self.check(items)["result"], "passed")
        wifi["frame_truncated"] = False
        self.assertIn("unreported_truncation", self.codes(self.check(items)))
        wifi.update(frame_truncated=True, original_len=2)
        self.assertEqual(self.check(items)["result"], "passed")
        wifi["captured_len"] = 1
        self.assertIn("raw_length_mismatch", self.codes(self.check(items)))

    def test_receive_gates_fail_empty_closed_session(self):
        items = [record("session_start", 1), record("session_end", 2)]
        self.assertEqual(self.check(items)["result"], "passed")
        report = self.check(items, min_lora_records=1, min_fresh_gps_records=1)
        self.assertTrue({"insufficient_lora_records", "insufficient_fresh_gps_records"}
                        .issubset(self.codes(report)))

    def test_json_corruption_duplicates_nonfinite_and_partial_final_record(self):
        self.write(session())
        original = self.path.read_bytes()
        for bad in (b'{"schema":1,"schema":1}\n', b'{"private":NaN}\n',
                    b'{"private":1e999}\n', b'{"private":"\xff"}\n', b"\n"):
            with self.subTest(bad=bad):
                self.path.write_bytes(original + bad)
                report = readback.validate_session(self.path, validator=self.validator)
                self.assertIn("invalid_json", self.codes(report))
                self.assertIn("missing_final_session_end", self.codes(report))
        self.path.write_bytes(original[:-1])
        self.assertIn("unterminated_record", self.codes(
            readback.validate_session(self.path, validator=self.validator)))
        self.path.write_bytes(original[:-20])
        self.assertIn("invalid_json", self.codes(
            readback.validate_session(self.path, validator=self.validator)))

    def test_empty_missing_oversized_and_error_cap_are_sanitized(self):
        self.path.write_bytes(b"")
        self.assertEqual(readback.validate_session(self.path, validator=self.validator)["result"], "failed")
        self.path.unlink()
        self.assertIn("input_read_failed", self.codes(readback.validate_session(
            self.path, validator=self.validator)))
        self.path.write_bytes(b"x" * (readback.MAX_LINE_BYTES + 5) + b"\n")
        self.assertIn("record_too_large", self.codes(readback.validate_session(
            self.path, validator=self.validator)))
        self.path.write_bytes(b"invalid private data\n" * 120)
        report = readback.validate_session(self.path, validator=self.validator)
        self.assertEqual(len(report["errors"]), readback.MAX_REPORTED_ERRORS)
        self.assertEqual(report["error_count"], 122)
        self.assertEqual(report["errors_omitted"], 22)

    def test_cli_multiple_files_and_schema_errors_do_not_disclose_values_or_paths(self):
        items = session()
        secret = "SECRET_TOKEN_PRIVATE_NETWORK_789"
        items[1]["wifi"]["ssid"] = secret * 2
        items[1]["gps"]["lat"] = 45.123456789
        items[1]["private-key-" + secret] = secret
        self.write(items)
        second = Path(self.temp.name) / (secret + ".jsonl")
        second.write_bytes(self.path.read_bytes())
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = readback.main([str(self.path), str(second), "--expected-target", TARGET])
        report = json.loads(output.getvalue())
        self.assertEqual(result, 1)
        self.assertEqual(report["files_checked"], 2)
        self.assertEqual(report["counts"]["lines"], 10)
        self.assertIn("schema_path", output.getvalue())
        for value in (secret, str(self.path), str(second), "45.123456789",
                      "02:00:00:00:00:01", "owned-test", "1234abcd"):
            self.assertNotIn(value, output.getvalue())
        self.write(session())
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(readback.main([str(self.path), "--expected-version", VERSION,
                                           "--min-lora-records", "1"]), 0)
        self.assertEqual(json.loads(output.getvalue())["result"], "passed")


if __name__ == "__main__":
    unittest.main()
