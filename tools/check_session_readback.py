#!/usr/bin/env python3
"""Strict, offline validation of copied native session JSONL files.

One complete, stopped session is required per file. Output never includes input
paths, record values, addresses, coordinates, or JSON validation messages.
--min-lora-records and --min-fresh-gps-records apply to EACH supplied session.
This proves file structure/readback, not capture origin, RF loss or GPS accuracy.
"""
import argparse
from collections import Counter
from datetime import datetime
import json
import math
from pathlib import Path
import sys

from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "schemas" / "observation-v1.schema.json"
# These are the production board::TARGET_ID -> board::NAME pairs. Markers
# currently record NAME, not TARGET_ID. A host test detects identity drift.
TARGET_BOARDS = {
    "cardputer-adv-lora1262": "Cardputer ADV + LoRa-1262 U214",
    "heltec-v4-r8-tft": "Heltec V4-R8 Expansion V2",
}
RADIO_TYPES = ("wifi", "ble", "lora")
COUNTER_NAMES = ("ingress_drops", "log_drops", "storage_errors", "radio_errors", "ui_skips")
MAX_LINE_BYTES = 65536  # Generous bound above the production 8 KiB write batch.
MAX_REPORTED_ERRORS = 100
FORMAT_CHECKER = FormatChecker()


@FORMAT_CHECKER.checks("date-time", raises=ValueError)
def _date_time(value):
    # Do not depend on jsonschema's optional RFC3339 extra being installed.
    # This schema permits only the writer's whole-second UTC/Z representation.
    if isinstance(value, str):
        datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    return True


def load_validator():
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema, format_checker=FORMAT_CHECKER)


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate key")
        result[key] = value
    return result


def _reject_constant(_value):
    raise ValueError("nonfinite constant")


def _finite_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("nonfinite number")
    return number


def _schema_pointer(path):
    # Use schema locations only, never instance paths (which can contain
    # untrusted/private property names) or ValidationError.message/instance.
    return "/" + "/".join(str(part).replace("~", "~0").replace("/", "~1") for part in path)


def validate_session(path, *, expected_target=None, expected_version=None,
                     min_lora_records=0, min_fresh_gps_records=0, validator=None):
    """Return a sanitized report; never return captured values or input paths."""
    if expected_target is not None and expected_target not in TARGET_BOARDS:
        raise ValueError("unknown expected target")
    if min_lora_records < 0 or min_fresh_gps_records < 0:
        raise ValueError("minimum record counts must be nonnegative")
    validator = validator or load_validator()
    errors = []
    error_count = 0
    counts = Counter(lines=0, records=0, session_start=0, session_end=0,
                     wifi=0, ble=0, lora=0, fresh_gps_radio_records=0,
                     fresh_utc_radio_records=0, fresh_gps_utc_radio_records=0)
    first = last = end_marker = None
    previous_sequence = 0
    last_record_line = 0

    def error(line, code, schema_path=None):
        nonlocal error_count
        error_count += 1
        if len(errors) < MAX_REPORTED_ERRORS:
            item = {"line": line, "code": code}
            if schema_path is not None:
                item["schema_path"] = schema_path
            errors.append(item)

    def check_raw(item, line):
        kind = item["type"]
        fields = {
            "wifi": ("raw_frame_hex", "captured_len", "frame_truncated"),
            "ble": ("payload_hex", "captured_len", "payload_truncated"),
            "lora": ("raw_hex", "length", "truncated"),
        }
        if kind not in fields:
            return
        payload = item[kind]
        raw_key, length_key, truncated_key = fields[kind]
        if raw_key not in payload:
            return  # A completed Wi-Fi AP scan has no retained frame.
        retained = len(payload[raw_key]) // 2
        if length_key in payload and payload[length_key] != retained:
            error(line, "raw_length_mismatch")
        original = payload.get("original_len")
        if original is not None:
            if original < retained:
                error(line, "original_length_below_retained")
            elif original > retained and payload.get(truncated_key) is False:
                error(line, "unreported_truncation")
        # A true truncation flag with equal lengths is allowed: the producer
        # can know bytes were lost even when their original count is unavailable.
        if kind == "ble" and payload.get("advertising_len", 0) > retained:
            error(line, "advertising_length_exceeds_retained")

    try:
        with Path(path).open("rb") as stream:
            while True:
                raw = stream.readline(MAX_LINE_BYTES + 1)
                if not raw:
                    break
                counts["lines"] += 1
                line = counts["lines"]
                if len(raw) > MAX_LINE_BYTES:
                    while raw and not raw.endswith(b"\n"):
                        raw = stream.readline(MAX_LINE_BYTES + 1)
                    error(line, "record_too_large")
                    continue
                if not raw.endswith(b"\n"):
                    error(line, "unterminated_record")
                try:
                    item = json.loads(raw.decode("utf-8"), object_pairs_hook=_object,
                                      parse_constant=_reject_constant, parse_float=_finite_float)
                except (ValueError, UnicodeDecodeError, RecursionError, OverflowError):
                    error(line, "invalid_json")
                    continue
                schema_errors = list(validator.iter_errors(item))
                if schema_errors:
                    for failure in schema_errors:
                        error(line, "schema_validation", _schema_pointer(failure.absolute_schema_path))
                    continue
                counts["records"] += 1
                kind = item["type"]
                counts[kind] += 1
                if item.get("synthetic") is True:
                    error(line, "synthetic_record")
                if line == 1:
                    if kind != "session_start":
                        error(line, "missing_initial_session_start")
                    else:
                        first = item
                elif kind == "session_start":
                    error(line, "unexpected_session_start")
                if end_marker is not None:
                    error(line, "record_after_session_end")
                if kind == "session_end":
                    end_marker = item
                if item["seq"] != previous_sequence + 1:
                    error(line, "noncontiguous_sequence")
                previous_sequence = item["seq"]
                if first is not None:
                    if item["session"] != first["session"]:
                        error(line, "session_mismatch")
                    if item.get("capture_mode", "wardriver") != first.get("capture_mode", "wardriver"):
                        error(line, "mode_mismatch")
                    for field in ("board", "firmware"):
                        if field in item and item[field] != first[field]:
                            error(line, field + "_mismatch")
                if expected_version is not None and "firmware" in item and item["firmware"] != expected_version:
                    error(line, "unexpected_firmware")
                if expected_target is not None and "board" in item and item["board"] != TARGET_BOARDS[expected_target]:
                    error(line, "unexpected_board")
                if "target" in item:
                    inferred_target = next((key for key, board in TARGET_BOARDS.items()
                                            if first is not None and first["board"] == board), None)
                    baseline_target = expected_target or inferred_target or (first or {}).get("target")
                    if not isinstance(item["target"], str) or (baseline_target is not None and item["target"] != baseline_target):
                        error(line, "target_mismatch")
                check_raw(item, line)
                if kind in RADIO_TYPES:
                    fresh_gps = item["gps"]["valid"]  # Schema enforces age <= 10 s.
                    utc_age = item.get("utc_age_ms")
                    fresh_utc = item["utc"] is not None and type(utc_age) is int and utc_age <= 1500
                    counts["fresh_gps_radio_records"] += int(fresh_gps)
                    counts["fresh_utc_radio_records"] += int(fresh_utc)
                    counts["fresh_gps_utc_radio_records"] += int(fresh_gps and fresh_utc)
                last = item
                last_record_line = line
    except OSError:
        error(counts["lines"], "input_read_failed")
    if first is None:
        error(1, "initial_session_start_unverified")
    if last is None or last["type"] != "session_end" or last_record_line != counts["lines"]:
        error(counts["lines"], "missing_final_session_end")
    if counts["lora"] < min_lora_records:
        error(0, "insufficient_lora_records")
    if counts["fresh_gps_radio_records"] < min_fresh_gps_records:
        error(0, "insufficient_fresh_gps_records")
    deltas = {}
    if first is not None and last is not None and last["type"] == "session_end":
        deltas = {name: (last["counts"][name] - first["counts"][name]) & 0xffffffff
                  for name in COUNTER_NAMES}
    return {"result": "failed" if error_count else "passed", "counts": dict(counts),
            "counter_deltas": deltas, "error_count": error_count,
            "errors": errors, "errors_omitted": error_count - len(errors)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+", type=Path)
    parser.add_argument("--expected-target", help="Production target ID; verifies the marker board name")
    parser.add_argument("--expected-version", help="Exact marker firmware version")
    parser.add_argument("--min-lora-records", type=int, default=0,
                        help="Minimum retained LoRa observations in each session")
    parser.add_argument("--min-fresh-gps-records", type=int, default=0,
                        help="Minimum radio observations with schema-valid fresh receiver fixes per session")
    args = parser.parse_args(argv)
    if args.expected_target is not None and args.expected_target not in TARGET_BOARDS:
        parser.error("Unknown expected target")
    if args.min_lora_records < 0 or args.min_fresh_gps_records < 0:
        parser.error("Minimum record counts must be nonnegative")
    validator = load_validator()
    reports = [dict(input_index=index, **validate_session(
        path, expected_target=args.expected_target, expected_version=args.expected_version,
        min_lora_records=args.min_lora_records, min_fresh_gps_records=args.min_fresh_gps_records,
        validator=validator)) for index, path in enumerate(args.sessions, 1)]
    totals = Counter()
    for report in reports:
        totals.update(report["counts"])
    passed = all(report["result"] == "passed" for report in reports)
    print(json.dumps({"result": "passed" if passed else "failed", "files_checked": len(reports),
                      "counts": dict(totals), "sessions": reports,
                      "scope": "Offline file structure/readback only; not capture origin, lossless RF reception or GPS accuracy."},
                     indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
