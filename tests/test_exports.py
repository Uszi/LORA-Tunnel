"""Host tests of interchange rules and corrupt-log recovery; no service calls."""
from collections import Counter
from contextlib import redirect_stderr
import copy
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "tools" / "export_logs.py"
spec = importlib.util.spec_from_file_location("export_logs", MODULE)
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


def wifi():
    return {"schema": 1, "type": "wifi", "utc": "2026-11-01T12:00:00Z",
            "gps": {"valid": True, "age_ms": 500, "lat": 1.5, "lon": 2.5,
                    "alt_m": None, "hdop": 0.9, "sats": 8},
            "wifi": {"bssid": "02:00:00:00:00:01", "ssid": 'Test, "quoted"\nname',
                     "security": "[WPA2-PSK-CCMP][ESS]", "channel": 6,
                     "frequency_mhz": 2437, "rssi": -65}}


def mesh():
    event = wifi()
    event.update(type="lora", lora={"rssi": -110},
                 mesh={"protocol": "meshtastic", "decode_status": "decoded",
                       "source_id": "ABCDEF12", "node_type": "CLIENT", "hop_count": 2,
                       "node_position": {"valid": True, "source": "node_advertised",
                                         "lat": 3.5, "lon": 4.5}})
    return event


class ExportTests(unittest.TestCase):
    def run_export(self, events, method=export.export_wigle, suffix=b"", **kwargs):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.jsonl"
            path.write_bytes(b"".join((json.dumps(e) + "\n").encode() for e in events) + suffix)
            output, counts = io.StringIO(), Counter()
            method(path, output, counts, **kwargs)
            return output.getvalue(), counts

    def test_current_columns_utc_escaping_unknown_accuracy(self):
        body, count = self.run_export([wifi()])
        rows = list(csv.reader(io.StringIO(body)))
        self.assertEqual(rows[1], list(export.WIGLE_COLUMNS))
        self.assertEqual(len(rows[2]), 14)
        self.assertEqual(rows[2][1], 'Test, "quoted"\\nname')
        self.assertEqual(rows[2][3], "2026-11-01 12:00:00")
        self.assertEqual(rows[2][9:11], ["", ""])
        self.assertEqual(count["exported"], 1)

    def test_reject_stale_invalid_missing_fixes_and_clock(self):
        events = []
        for key, value in [("valid", False), ("age_ms", 10001), ("age_ms", None),
                           ("lat", 91), ("lon", float("nan")), ("lat", True)]:
            event = wifi()
            event["gps"][key] = value
            events.append(event)
        for stamp in (None, "2026-11-01T12:00:00", "garbage"):
            event = wifi()
            event["utc"] = stamp
            events.append(event)
        _, count = self.run_export(events)
        self.assertEqual(count["exported"], 0)
        self.assertEqual(count["skipped_missing_stale_invalid_receiver_fix"], 6)
        self.assertEqual(count["skipped_invalid_utc"], 3)

    def test_timezone_normalizes_and_real_zero_coordinate_allowed(self):
        event = wifi()
        event["utc"] = "2026-11-01T07:00:00-05:00"
        event["gps"].update(lat=0, lon=0)
        row = export.wigle_row(event, Counter(), 10000, False)
        self.assertEqual(row[3], "2026-11-01 12:00:00")
        self.assertEqual(row[7:9], [0, 0])

    def test_wifi_channel_frequency_must_match(self):
        event = wifi()
        event["wifi"]["frequency_mhz"] = 5180
        self.assertIsNone(export.wigle_row(event, Counter(), 10000, False))

    def test_recon_and_unknown_modes_never_become_wigle_inventory(self):
        events = [wifi()]
        explicit = wifi()
        explicit["capture_mode"] = "wardriver"
        events.append(explicit)
        for mode in ("recon", "future_mode", None):
            event = wifi()
            event["capture_mode"] = mode
            events.append(event)
        ble = wifi()
        ble.update(type="ble", capture_mode="recon",
                   ble={"address": "02:00:00:00:00:02", "address_type": "public",
                        "name": "Synthetic iBeacon", "rssi": -60})
        events.append(ble)
        body, counts = self.run_export(events)
        self.assertEqual(len(list(csv.reader(io.StringIO(body)))), 4)
        self.assertEqual(counts["exported"], 2)
        self.assertEqual(counts["skipped_nonwardriver_mode"], 4)

    def test_raw_wifi_frames_cannot_export_as_access_points(self):
        # Exclude raw frames even if an older producer incorrectly provided a
        # BSSID, omitted capture_mode, or labelled the session wardriver.
        for metadata in ({"wifi_frame": True}, {"raw_frame_hex": "80" + "00" * 1023},
                         {"wifi_frame": "false"}):
            with self.subTest(metadata=metadata):
                event = wifi()
                event["wifi"].update(metadata)
                event["wifi"].update(tx_address="02:00:00:00:00:03", original_len=1500,
                                     captured_len=1024, frame_truncated=True)
                original = copy.deepcopy(event)
                counts = Counter()
                self.assertIsNone(export.wigle_row(event, counts, 10000, False))
                self.assertEqual(counts["skipped_raw_wifi_frame"], 1)
                self.assertEqual(event, original)

    def test_streaming_reader_preserves_bounded_raw_recon_and_field_provenance(self):
        event = wifi()
        event["capture_mode"] = "recon"
        event["wifi"].pop("bssid")
        event["wifi"].update(wifi_frame=True, tx_address="02:00:00:00:00:03",
                             raw_frame_hex="80" + "00" * 1023,
                             original_len=1500, captured_len=1024, frame_truncated=True)
        event["recon"] = {"category": "Drone / Remote ID", "confidence": "Strong signature",
                          "evidence": "Synthetic decoded Remote ID", "label": "Drone",
                          "vendor": "Remote ID broadcast", "payload_valid": True,
                          "remote_id": {"source": "self_reported_broadcast", "authenticated": False,
                                        "id": "fixture", "id_type": 1, "aircraft_type": 2,
                                        "operator_latitude": 3.5, "operator_longitude": 4.5,
                                        "operator_location_type": 0}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recon.jsonl"
            path.write_text(json.dumps(event) + "\n", encoding="utf-8")
            records = list(export.records(path, Counter()))
        self.assertEqual(records, [event])
        self.assertEqual(len(records[0]["wifi"]["raw_frame_hex"]), 2048)
        self.assertNotIn("bssid", records[0]["wifi"])
        self.assertNotIn("latitude", records[0]["recon"]["remote_id"])
        self.assertEqual(records[0]["gps"]["lat"], 1.5)

    def test_mesh_export_excludes_recon_even_with_decoded_mesh_fields(self):
        legacy, wardriver, recon = mesh(), mesh(), mesh()
        wardriver["capture_mode"] = "wardriver"
        recon["capture_mode"] = "recon"
        body, counts = self.run_export([legacy, wardriver, recon], export.export_mesh)
        self.assertEqual(len(json.loads(body)["meshcore_nodes"]), 2)
        self.assertEqual(counts["skipped_nonwardriver_mode"], 1)

    def test_sanitized_ssid_recovers_exact_utf8_or_exports_blank(self):
        for original, expected in [("Café, \"test\"".encode().hex(), 'Café, "test"'),
                                   ("ff616263", ""), ("edb080", ""), ("c080", ""),
                                   ("00", ""), ("00 " , ""), ("g0", ""),
                                   ("61" * 33, ""), (None, "")]:
            with self.subTest(original=original):
                event = wifi()
                event["wifi"].update(ssid="Invented? display name", ssid_sanitized=True, ssid_hex=original)
                counts = Counter()
                row = export.wigle_row(event, counts, 10000, False)
                self.assertEqual(row[1], expected)
                counter = "ssid_recovered_from_raw" if expected else "ssid_blank_invalid_encoding"
                self.assertEqual(counts[counter], 1)

    def test_sanitized_ble_name_exports_blank_and_keeps_record(self):
        event = wifi()
        event.update(type="ble", ble={"address": "02:00:00:00:00:02", "address_type": "public",
                                      "name": "Invented? name", "name_sanitized": True,
                                      "rssi": -70, "payload_hex": "0309ff61"})
        counts = Counter()
        row = export.wigle_row(event, counts, 10000, False)
        self.assertEqual(row[1], "")
        self.assertEqual(row[-1], "BLE")
        self.assertEqual(counts["ble_name_blank_invalid_encoding"], 1)
        self.assertEqual(event["ble"]["payload_hex"], "0309ff61")

    def test_current_logger_contract_and_header_only_lora(self):
        # Mirrors src/logger.cpp field shapes, including absent manufacturer ID,
        # nullable UTC/fix fields, and header candidates without a decoded mesh.
        public_ble = wifi()
        public_ble.update(type="ble", ble={"address": "02:00:00:00:00:05",
                         "address_type": "public_identity", "name": "Logger fixture",
                         "rssi": -72, "payload_hex": "020106", "payload_truncated": False})
        raw_lora = wifi()
        raw_lora.update(type="lora", lora={"frequency_mhz": 906.875,
                         "bandwidth_khz": 250, "spreading_factor": 11,
                         "coding_rate_denominator": 5, "sync_word": 43,
                         "preamble_symbols": 16, "profile": "US LongFast",
                         "lna": False, "rssi": -110, "snr": 3.5, "length": 20,
                         "crc_status": "not_reported", "payload_status": "not_decoded", "raw_hex": "00" * 20,
                         "classification": "meshtastic_header_candidate",
                         "candidate_header": {"from": "abcdef12", "to": "ffffffff",
                                              "packet_id": "00000001", "hop_start": 3,
                                              "hop_limit": 2}})
        body, count = self.run_export([public_ble, raw_lora])
        row = list(csv.reader(io.StringIO(body)))[2]
        self.assertEqual(row[-2:], ["", "BLE"])
        self.assertEqual(count["exported"], 1)
        candidate, mesh_count = self.run_export([raw_lora], export.export_mesh)
        self.assertEqual(json.loads(candidate)["meshcore_nodes"], [])
        self.assertEqual(mesh_count["skipped_not_decoded_mesh"], 1)

    def test_ble_address_policy_and_no_invented_frequency(self):
        event = wifi()
        event.update(type="ble", ble={"address": "02:00:00:00:00:02", "address_type": "public",
                                      "name": "Synthetic beacon", "rssi": -70, "manufacturer_id": 76})
        row = export.wigle_row(event, Counter(), 10000, False)
        self.assertEqual(row[4:6], ["", ""])
        self.assertEqual(row[-2:], [76, "BLE"])
        event["ble"]["address_type"] = "random_resolvable"
        self.assertIsNone(export.wigle_row(event, Counter(), 10000, False))
        counts = Counter()
        self.assertIsNotNone(export.wigle_row(event, counts, 10000, True))
        self.assertEqual(counts["included_random_ble"], 1)

    def test_corrupt_interrupted_and_oversize_lines_recover(self):
        suffix = b"{bad json}\n" + b"x" * (export.MAX_LINE_BYTES + 4) + b"\n"
        suffix += (json.dumps(wifi()) + "\n").encode() + b'{"schema":1,"type":'
        _, count = self.run_export([wifi()], suffix=suffix)
        self.assertEqual(count["exported"], 2)
        self.assertEqual(count["skipped_malformed_json"], 2)
        self.assertEqual(count["skipped_oversize"], 1)

    def test_wrong_schema_and_non_objects(self):
        wrong = wifi()
        wrong["schema"] = True
        _, count = self.run_export([[], None, wrong])
        self.assertEqual(count["skipped_schema"], 3)

    def test_mesh_uses_advertised_node_fix_not_receiver_fix(self):
        event = mesh()
        event["gps"]["valid"] = False
        body, count = self.run_export([event], export.export_mesh)
        node = json.loads(body)["meshcore_nodes"][0]
        self.assertEqual((node["lat"], node["lon"]), (3.5, 4.5))
        self.assertEqual(node["node_id"], "abcdef12")
        self.assertEqual(node["type"], "MESHCORE")
        self.assertEqual(node["network"], "meshtastic")
        self.assertEqual(node["path_hops"], 2)
        self.assertEqual(count["exported"], 1)

    def test_mesh_rejects_unknown_hops_or_receiver_position(self):
        event = mesh()
        event["mesh"].pop("hop_count")
        self.assertIsNone(export.mesh_record(event, Counter()))
        event = mesh()
        event["mesh"]["node_position"]["source"] = "receiver_gps"
        self.assertIsNone(export.mesh_record(event, Counter()))
        event = mesh()
        event["mesh"]["node_position"]["lat"] = 100
        self.assertIsNone(export.mesh_record(event, Counter()))

    def test_meshcore_full_key_and_exact_id_prefix(self):
        event = mesh()
        event["mesh"].update(protocol="meshcore", node_type="Repeater", public_key="a1" * 32,
                             source_id="a1" * 8)
        row = export.mesh_record(event, Counter())
        self.assertEqual(row["public_key"], "a1" * 32)
        bad = copy.deepcopy(event)
        bad["mesh"]["source_id"] = "b1" * 8
        self.assertIsNone(export.mesh_record(bad, Counter()))

    def test_malformed_nested_types_and_huge_number_do_not_crash(self):
        for field in ("protocol", "node_type"):
            event = mesh()
            event["mesh"][field] = ["bad"]
            self.assertIsNone(export.mesh_record(event, Counter()))
        event = wifi()
        event["gps"]["lat"] = 10 ** 300
        self.assertIsNone(export.wigle_row(event, Counter(), 10000, False))

    def test_cli_refuses_overwriting_original_and_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.jsonl"
            path.write_text(json.dumps(wifi()) + "\n", encoding="utf-8")
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                export.main(["wigle", str(path), str(path)])
            output = Path(directory) / "existing.csv"
            output.write_text("keep", encoding="utf-8")
            with redirect_stderr(io.StringIO()):
                self.assertEqual(export.main(["wigle", str(path), str(output)]), 1)
            self.assertEqual(output.read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
