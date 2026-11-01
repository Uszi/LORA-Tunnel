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
BOOLEAN_FIELDS = {"ready", "running", "saving", "sd", "touch", "lora", "gps_fix"}
DIAGNOSTICS = (
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
        if key not in STATUS_FIELDS:
            continue  # Never echo unknown future fields that might be private.
        if key in values:
            return None
        if key == "version":
            if not re.fullmatch(r"[0-9][A-Za-z0-9._+-]{0,47}", value):
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


def state_matches(command, status):
    if command == "start":
        return status["running"] == 1
    if command == "stop":
        return status["running"] == 0 and status["saving"] == 0
    return True


def run_bench(connection, command, seconds, *, clock=time.monotonic,
              sleep=time.sleep, emit=print):
    """Use an already-open connection; injected serial/clock fakes need no port.

    This protocol has no transaction IDs. The available acknowledgment is a
    complete status after input is cleared and the command is written. Send
    start/stop once, then poll only status for the bounded observation interval.
    """
    omitted = malformed = oversize = accepted = 0
    pending = bytearray()
    discarding = False
    latest = latest_at = None

    def finish(code, reason):
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
            if now >= next_poll:
                send("status")
                next_poll = now + POLL_INTERVAL_SECONDS
            for byte in connection.read(1024):
                if byte != 10:
                    if not discarding:
                        if len(pending) >= MAX_LINE_BYTES:
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
                        continue
                    if latest is not None and status["uptime_ms"] < latest["uptime_ms"]:
                        return finish(4, "device uptime decreased after command")
                    latest = status
                    latest_at = clock()
                    accepted += 1
                    emit("LT STATUS " + " ".join(f"{key}={status[key]}" for key in STATUS_FIELDS))
                else:
                    omitted += 1
        if discarding:
            oversize += 1
        elif pending:
            omitted += 1  # A partial line cannot acknowledge a command.
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
