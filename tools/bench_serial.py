#!/usr/bin/env python3
"""Bounded USB bench commands for LORA Tunnel; no flashing or credential access."""
import argparse
import re
import time
import serial


MAX_LINE_BYTES = 4096
POLL_INTERVAL_SECONDS = 1.0
STATUS_MAX_AGE_SECONDS = 2.5
STATUS_FIELDS = (
    "version", "uptime_ms", "ready", "running", "saving", "sd", "touch", "lora",
    "gps_chars", "gps_fix", "sats", "wifi", "ble", "lora_rx", "drops", "errors", "free_heap",
)
OPTIONAL_STATUS_FIELDS = ("target", "min_internal", "largest_internal", "loop_stack", "writer_stack", "ui_p95_ms", "ui_max_ms", "ui_samples", "upload", "input_ok", "cap_ok", "init_writer", "init_scanner", "init_memory", "scanner_stage", "heap_hw", "heap_ui", "heap_config", "heap_before_scan", "mode", "matches", "audio_ok", "tone_active", "alerted", "alert_full", "alloc_hook", "alloc_fail", "upload_runs", "upload_min_free", "upload_largest", "upload_stack", "upload_check", "upload_ready_free", "upload_ready_block")
BOOLEAN_FIELDS = {"alloc_hook", "alloc_fail", "audio_ok", "tone_active", "alert_full", "input_ok", "cap_ok", "init_writer", "init_scanner", "init_memory", "upload","ready", "running", "saving", "sd", "touch", "lora", "gps_fix"}
RADIO_FIELDS = ("mode", "protocol", "frequency_khz", "bandwidth_hz", "sf", "cr", "sync",
                "remaining_ms", "switches", "deferrals", "timeouts", "interruptions",
                "mt_listen_ms", "mc_listen_ms", "mt_rx", "mc_rx", "decoded", "other")
DIAGNOSTICS = (
    ("watchdog", re.compile(r"\b(?:task_wdt:\s*(?:Task watchdog got triggered|Aborting)|Interrupt wdt timeout on CPU[0-9]*)\b", re.IGNORECASE)),
    ("panic", re.compile(r"guru meditation|\bpanic\b", re.IGNORECASE)),
    ("brownout", re.compile(r"brownout", re.IGNORECASE)),
    ("assertion", re.compile(r"assert(?:ion)?\s+(?:failed|failure)|assertion.*failed", re.IGNORECASE)),
    ("reboot", re.compile(r"rebooting|\brst:\s*0x|esp-rom:", re.IGNORECASE)),
)


def parse_status(line):
    """Validate the complete firmware status contract; retain only public fields."""
    if not line.startswith("LT STATUS "):
        return None
    values = {}
    for token in line[len("LT STATUS "):].split():
        if "=" not in token:
            return None
        key, value = token.split("=", 1)
        if key not in STATUS_FIELDS + OPTIONAL_STATUS_FIELDS:
            continue  # Never echo unknown future fields that might be private.
        if key in values:
            return None
        if key == "mode":
            if value not in ("wardriver", "recon"):
                return None
            values[key] = value
        elif key in ("version", "target", "scanner_stage"):
            pattern = (r"[0-9][A-Za-z0-9._+-]{0,47}" if key == "version" else
                       r"[a-z][a-z0-9_]{0,31}" if key == "scanner_stage" else
                       r"[a-z0-9][a-z0-9-]{0,47}")
            if not re.fullmatch(pattern, value):
                return None
            values[key] = value
        else:
            if not re.fullmatch(r"[0-9]{1,10}", value):
                return None
            number = int(value)
            ceiling = 1 if key in BOOLEAN_FIELDS else (65535 if key == "sats" else 4294967295)
            if number > ceiling:
                return None
            values[key] = number
    return values if all(key in values for key in STATUS_FIELDS) else None


def parse_radio(line):
    """Allowlisted numeric receiver telemetry; never echo unknown private fields."""
    if not line.startswith("LT RADIO "):
        return None
    values = {}
    for token in line[len("LT RADIO "):].split():
        if "=" not in token:
            return None
        key, value = token.split("=", 1)
        if key not in RADIO_FIELDS:
            continue
        if key in values or not re.fullmatch(r"[0-9]{1,10}", value):
            return None
        number = int(value)
        ceiling = {"mode": 2, "protocol": 2, "sf": 12, "cr": 8, "sync": 255}.get(key, 4294967295)
        if number > ceiling:
            return None
        values[key] = number
    return values if all(key in values for key in RADIO_FIELDS) else None


def state_matches(command, status):
    if command == "start":
        return status["running"] == 1
    if command == "stop":
        return status["running"] == 0 and status["saving"] == 0
    return True


def run_bench(connection, command, seconds, *, clock=time.monotonic,
              sleep=time.sleep, emit=print, strict=False, observation=None, on_status=None):
    """Use an already-open connection; injected serial/clock fakes need no port.

    This protocol has no transaction IDs. The available acknowledgment is a
    complete status after input is cleared and the command is written. Send
    start/stop once, then poll only status for the bounded observation interval.
    """
    # Strict continuous-observation checks are opt-in for long acceptance runs;
    # short start/stop commands still allow their normal transition interval.
    omitted = malformed = oversize = accepted = 0
    pending = bytearray()
    discarding = False
    latest = latest_at = first_at = None
    started = None
    max_gap = 0.0

    def finish(code, reason):
        if observation is not None:
            elapsed = max(0.0, clock() - started) if started is not None else 0.0
            observation.update(result=code, reason=reason, strict=strict,
                elapsed_seconds=round(elapsed, 6), valid_status=accepted,
                malformed_status=malformed, oversize_lines=oversize,
                other_lines_omitted=omitted, max_status_gap_seconds=round(max_gap, 6),
                first_status_seconds=None if first_at is None else round(first_at-started, 6),
                last_status_seconds=None if latest_at is None else round(latest_at-started, 6))
        emit(f"Bench {'PASS' if code == 0 else 'FAIL'}: {reason}; "
             f"valid_status={accepted} malformed_status={malformed} "
             f"oversize_lines={oversize} other_lines_omitted={omitted}")
        return code

    def send(request):
        payload = ("tunnel " + request + "\n").encode("ascii")
        if connection.write(payload) != len(payload):
            raise serial.SerialTimeoutException("Incomplete command write")

    try:
        sleep(0.25)
        connection.reset_input_buffer()
        send(command)
        started = clock()
        deadline = started + seconds
        next_poll = started + POLL_INTERVAL_SECONDS
        while clock() < deadline:
            now = clock()
            if strict and now - (latest_at if latest_at is not None else started) > STATUS_MAX_AGE_SECONDS:
                max_gap = max(max_gap, now - (latest_at if latest_at is not None else started))
                return finish(3, "continuous status polling gap exceeded limit")
            if now >= next_poll:
                send("status")
                next_poll = now + POLL_INTERVAL_SECONDS
            for byte in connection.read(1024):
                if byte != 10:
                    if not discarding:
                        if len(pending) >= MAX_LINE_BYTES:
                            if strict and bytes(pending).lstrip().startswith(b"LT STATUS"):
                                oversize += 1
                                return finish(3, "oversize required status line")
                            pending.clear()
                            discarding = True
                        else:
                            pending.append(byte)
                    continue
                if discarding:
                    oversize += 1
                    pending.clear()
                    discarding = False
                    continue
                raw = bytes(pending)
                pending.clear()
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                for category, pattern in DIAGNOSTICS:
                    if pattern.search(line):
                        # Do not echo crash dumps or arbitrary private data.
                        return finish(4, f"device {category} diagnostic after command")
                if line.startswith("LT STATUS"):
                    status = parse_status(line)
                    if status is None:
                        malformed += 1
                        if strict:
                            return finish(3, "malformed required status line")
                        continue
                    if latest is not None and status["uptime_ms"] < latest["uptime_ms"]:
                        return finish(4, "device uptime decreased after command")
                    if strict and latest is not None and status["uptime_ms"] == latest["uptime_ms"]:
                        return finish(4, "device uptime did not advance between statuses")
                    received_at = clock()
                    gap = received_at - (latest_at if latest_at is not None else started)
                    max_gap = max(max_gap, gap)
                    if strict and gap > STATUS_MAX_AGE_SECONDS:
                        return finish(3, "continuous status polling gap exceeded limit")
                    latest = status
                    latest_at = received_at
                    if first_at is None:
                        first_at = received_at
                    accepted += 1
                    # At most twice the polling rate plus startup/end allowance.
                    # Bound saved evidence even if a faulty device floods statuses.
                    if strict and accepted > int(seconds / POLL_INTERVAL_SECONDS) * 2 + 2:
                        return finish(3, "status count exceeded bounded observation limit")
                    if on_status is not None:
                        on_status(dict(status), round(received_at - started, 6))
                    else:
                        emit("LT STATUS " + " ".join(f"{key}={status[key]}" for key in STATUS_FIELDS + OPTIONAL_STATUS_FIELDS if key in status))
                elif line.startswith("LT RADIO "):
                    radio = parse_radio(line)
                    if radio is not None and latest is not None:
                        emit("LT RADIO " + " ".join(f"{key}={radio[key]}" for key in RADIO_FIELDS))
                    else:
                        omitted += 1
                else:
                    omitted += 1
        if discarding:
            oversize += 1
        elif pending:
            if strict and bytes(pending).lstrip().startswith(b"LT STATUS"):
                malformed += 1
                return finish(3, "incomplete required status line at observation end")
            omitted += 1  # A partial line cannot acknowledge a command.
        if started is not None:
            max_gap = max(max_gap, clock() - (latest_at if latest_at is not None else started))
        if latest is None:
            return finish(2, "no valid post-command status acknowledgment before timeout")
        if clock() - latest_at > STATUS_MAX_AGE_SECONDS:
            return finish(3, "status polling timed out after the last valid acknowledgment")
        if not state_matches(command, latest):
            expected = "running=1" if command == "start" else "running=0 saving=0"
            return finish(3, f"command did not reach {expected} before timeout")
        return finish(0, "post-command status and requested state verified")
    except (OSError, serial.SerialException):
        return finish(1, "serial communication failed")
    except KeyboardInterrupt:
        return finish(1, "operator interrupted observation")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--command", choices=("status", "start", "stop"), default="status")
    parser.add_argument("--seconds", type=int, default=10)
    args = parser.parse_args(argv)
    if not 1 <= args.seconds <= 60:
        parser.error("seconds must be between 1 and 60")
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
    # Avoid the conventional automatic bootloader-reset sequence.
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        with connection:
            return run_bench(connection, args.command, args.seconds)
    except (OSError, serial.SerialException):
        print("Bench FAIL: could not open or close the specified serial port", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
