#!/usr/bin/env python3
"""Read-only, bounded LORA Tunnel GNSS telemetry; never prints raw NMEA or coordinates."""
import argparse
import re
import time

import serial
from bench_serial import DIAGNOSTICS, MAX_LINE_BYTES, STATUS_FIELDS, parse_status


POLL_INTERVAL_SECONDS = 3.0
RESPONSE_MAX_AGE_SECONDS = 4.5
RECEIVER_MAX_AGE_MS = 5000
GPS_FIELDS = (
    "baud", "bytes", "checksum_ok", "checksum_failed", "sentences_with_fix",
    "gga", "rmc", "gsa", "gsv", "other", "fix_quality", "rmc_status", "fix_type",
    "satellites_visible", "sentence_age_ms", "location_age_ms", "parser_location_valid",
    "date_valid", "time_valid", "gsv_age_ms", "txt", "antenna_open_reports",
    "antenna_open_age_ms", "output_config_attempted", "output_config_sent",
    "antenna_status", "antenna_ok_reports", "antenna_status_age_ms",
)
SKY_FIELDS = (
    "talker", "sentences", "complete_groups", "age_ms", "visible", "max_cn0",
    "cn0_count", "positive_cn0_count", "signal_id",
)
TALKERS = {"GP", "GB", "BD", "GL", "GN"}
GPS_BOOLEAN_FIELDS = {
    "parser_location_valid", "date_valid", "time_valid",
    "output_config_attempted", "output_config_sent",
}
GPS_RANGES = {"fix_quality": (-1, 255), "fix_type": (-1, 255),
              "satellites_visible": (-1, 255), "antenna_status": (-1, 1)}
SKY_RANGES = {"visible": (-1, 99), "max_cn0": (-1, 99),
              "cn0_count": (0, 36), "positive_cn0_count": (0, 36), "signal_id": (0, 255)}


def parse_diagnostic(line, prefix, fields, ranges, boolean_fields=()):
    """Allow only the firmware's public numeric fields and explicit enums."""
    if not line.startswith(prefix):
        return None
    values = {}
    for token in line[len(prefix):].split():
        if "=" not in token:
            return None
        key, value = token.split("=", 1)
        if key not in fields:
            continue
        if key in values:
            return None
        if key == "talker":
            if value not in TALKERS:
                return None
            values[key] = value
        elif key == "rmc_status":
            if value not in ("A", "V", "?"):
                return None
            values[key] = value
        else:
            if not re.fullmatch(r"-?[0-9]{1,10}", value):
                return None
            number = int(value)
            low, high = (0, 1) if key in boolean_fields else ranges.get(key, (0, 4294967295))
            if not low <= number <= high:
                return None
            if key == "signal_id" and number not in (*range(16), 255):
                return None
            values[key] = number
    if not all(key in values for key in fields):
        return None
    if "cn0_count" in values and values["positive_cn0_count"] > values["cn0_count"]:
        return None
    return values


def parse_gps(line):
    return parse_diagnostic(line, "LT GPS ", GPS_FIELDS, GPS_RANGES, GPS_BOOLEAN_FIELDS)


def parse_sky(line):
    return parse_diagnostic(line, "LT SKY ", SKY_FIELDS, SKY_RANGES)


def run_bench(connection, seconds, *, clock=time.monotonic, sleep=time.sleep, emit=print):
    """Only sends status/GPS queries; injection supports a port-free smoke check."""
    pending = bytearray()
    accepted_status = accepted_gps = accepted_sky = malformed = omitted = 0
    latest_status = latest_gps = None
    status_at = gps_at = None

    def finish(code, reason):
        emit(f"GPS bench {'PASS' if code == 0 else 'FAIL'}: {reason}; "
             f"valid_status={accepted_status} valid_gps={accepted_gps} "
             f"valid_sky={accepted_sky} malformed_diagnostics={malformed} "
             f"other_lines_omitted={omitted}")
        return code

    def query():
        payload = b"tunnel status\ntunnel gps\n"
        if connection.write(payload) != len(payload):
            raise serial.SerialTimeoutException("Incomplete query write")

    def display(prefix, values, fields):
        emit(prefix + " ".join(f"{key}={values[key]}" for key in fields))

    try:
        sleep(0.25)
        connection.reset_input_buffer()
        started = clock()
        deadline = started + seconds
        query()
        next_poll = started + POLL_INTERVAL_SECONDS
        while clock() < deadline:
            now = clock()
            if now >= next_poll:
                query()
                next_poll = now + POLL_INTERVAL_SECONDS
            for byte in connection.read(1024):
                if byte != 10:
                    if len(pending) >= MAX_LINE_BYTES:
                        return finish(4, "serial line exceeded the bounded input limit")
                    pending.append(byte)
                    continue
                line = bytes(pending).decode("utf-8", errors="replace").strip()
                pending.clear()
                if not line:
                    continue
                for category, pattern in DIAGNOSTICS:
                    if pattern.search(line):
                        return finish(4, f"device {category} diagnostic after query")
                if line.startswith("LT STATUS"):
                    status = parse_status(line)
                    if status is None:
                        malformed += 1
                        continue
                    if latest_status is not None and status["uptime_ms"] < latest_status["uptime_ms"]:
                        return finish(4, "device uptime decreased during observation")
                    latest_status, status_at = status, clock()
                    accepted_status += 1
                    display("LT STATUS ", status, STATUS_FIELDS)
                elif line.startswith("LT GPS"):
                    gps = parse_gps(line)
                    if gps is None:
                        malformed += 1
                        continue
                    if latest_gps is not None and gps["bytes"] < latest_gps["bytes"]:
                        return finish(4, "receiver byte counter decreased during observation")
                    latest_gps, gps_at = gps, clock()
                    accepted_gps += 1
                    display("LT GPS ", gps, GPS_FIELDS)
                elif line.startswith("LT SKY"):
                    sky = parse_sky(line)
                    if sky is None:
                        malformed += 1
                        continue
                    accepted_sky += 1
                    display("LT SKY ", sky, SKY_FIELDS)
                else:
                    omitted += 1
        if pending:
            omitted += 1
        if latest_status is None or latest_gps is None:
            return finish(2, "missing valid post-query status or GPS response")
        now = clock()
        if now - status_at > RESPONSE_MAX_AGE_SECONDS or now - gps_at > RESPONSE_MAX_AGE_SECONDS:
            return finish(3, "status or GPS response became stale")
        if (latest_gps["checksum_ok"] == 0 or
                latest_gps["sentence_age_ms"] + int((now - gps_at) * 1000) > RECEIVER_MAX_AGE_MS):
            return finish(3, "no recent checksum-valid receiver sentence")
        return finish(0, f"fresh diagnostics received; gps_fix={latest_status['gps_fix']} "
                      "(this result does not require a position fix)")
    except (OSError, serial.SerialException):
        return finish(1, "serial communication failed")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=15)
    args = parser.parse_args(argv)
    if not 1 <= args.seconds <= 60:
        parser.error("seconds must be between 1 and 60")
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=1)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        with connection:
            return run_bench(connection, args.seconds)
    except (OSError, serial.SerialException):
        print("GPS bench FAIL: could not open or close the specified serial port", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
