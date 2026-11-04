#!/usr/bin/env python3
"""Read only allowlisted runtime metrics during a manually started capture."""
import argparse
import json
from pathlib import Path
import re
import sys

import serial
from bench_serial import OPTIONAL_STATUS_FIELDS, STATUS_FIELDS, STATUS_MAX_AGE_SECONDS, run_bench

TARGET = "cardputer-adv-lora1262"
MAX_SAMPLES = 7202  # Two statuses/second for the maximum one-hour run, plus allowance.
COUNTERS = ("uptime_ms", "gps_chars", "wifi", "ble", "lora_rx", "drops", "errors", "ui_samples", "matches")
SAMPLE_FIELDS = STATUS_FIELDS + OPTIONAL_STATUS_FIELDS + ("elapsed_seconds",)


def app_sha256(value):
    if not re.fullmatch(r"[0-9a-fA-F]{64}", value):
        raise argparse.ArgumentTypeError("expected app SHA-256 must contain exactly 64 hexadecimal characters")
    return value.lower()


def evaluate(samples, serial_result, seconds, *, observation=None, expected_app_sha256=None, expected_mode="wardriver"):
    observation = observation or {}
    # Retain only the fixed public contract, even for callers using test fixtures.
    sample_count = len(samples)
    samples = [{key: sample[key] for key in SAMPLE_FIELDS if key in sample}
               for sample in samples[:MAX_SAMPLES]]
    complete = bool(samples) and all(all(key in s for key in STATUS_FIELDS) for s in samples)
    timed = bool(samples) and all(isinstance(s.get("elapsed_seconds"), (int, float)) for s in samples)
    ordered = timed and all(b["elapsed_seconds"] > a["elapsed_seconds"] for a, b in zip(samples, samples[1:]))
    gaps = ([b["elapsed_seconds"] - a["elapsed_seconds"] for a, b in zip(samples, samples[1:])]
            if ordered else [])
    checks = {
        "serial": serial_result == 0 and observation.get("result") == 0,
        "duration": seconds >= 1800 and observation.get("elapsed_seconds", 0) >= seconds,
        "samples": int(seconds * 0.9) <= sample_count <= MAX_SAMPLES,
        "complete_status": complete,
        "continuous_status": bool(ordered and observation.get("strict") is True
            and observation.get("malformed_status") == 0
            and observation.get("valid_status") == sample_count
            and 0 <= samples[0]["elapsed_seconds"] <= STATUS_MAX_AGE_SECONDS
            and seconds - STATUS_MAX_AGE_SECONDS <= samples[-1]["elapsed_seconds"] <= observation.get("elapsed_seconds", 0)
            and max(gaps, default=0) <= STATUS_MAX_AGE_SECONDS
            and observation.get("max_status_gap_seconds", float("inf")) <= STATUS_MAX_AGE_SECONDS),
    }
    checks["target"] = bool(samples) and all(s.get("target") == TARGET for s in samples)
    checks["version_consistent"] = complete and len({s["version"] for s in samples}) == 1
    enabled = ("ready", "running", "sd", "input_ok", "cap_ok", "init_writer", "init_scanner", "init_memory")
    checks["capture"] = bool(samples) and all(all(s.get(key) == 1 for key in enabled)
        and s.get("saving") == 0 and s.get("upload") == 0 and s.get("mode") == expected_mode for s in samples)
    checks["counter_continuity"] = complete and all(
        all(key in s for s in samples) and all(b[key] >= a[key] for a, b in zip(samples, samples[1:]))
        for key in COUNTERS)
    checks["uptime_advances"] = complete and all(b["uptime_ms"] > a["uptime_ms"] for a, b in zip(samples, samples[1:]))
    limits = {"min_internal": 32768, "largest_internal": 16384, "loop_stack": 1024, "writer_stack": 1024}
    for name, limit in limits.items():
        checks[name] = bool(samples) and all(s.get(name, -1) >= limit for s in samples)
    checks["ui_samples"] = bool(samples) and samples[-1].get("ui_samples", 0) - samples[0].get("ui_samples", 0) >= 30
    checks["ui_p95"] = bool(samples) and all(0 <= s.get("ui_p95_ms", -1) <= 100 for s in samples)
    checks["ui_max"] = bool(samples) and all(0 <= s.get("ui_max_ms", -1) <= 250 for s in samples)
    metrics = {key: min(s[key] for s in samples) for key in limits
               if samples and all(key in s for s in samples)}
    metrics["sample_count"] = sample_count
    if timed:
        metrics["sample_span_seconds"] = round(samples[-1]["elapsed_seconds"] - samples[0]["elapsed_seconds"], 6)
        metrics["max_sample_gap_seconds"] = round(max(gaps, default=0), 6) if ordered else None
    if complete:
        metrics.update(drop_delta=samples[-1]["drops"] - samples[0]["drops"],
                       error_initial=samples[0]["errors"], error_final=samples[-1]["errors"],
                       error_peak=max(s["errors"] for s in samples),
                       error_delta=samples[-1]["errors"] - samples[0]["errors"],
                       gps_fix_samples=sum(s["gps_fix"] for s in samples),
                       lora_rx_delta=samples[-1]["lora_rx"] - samples[0]["lora_rx"])
        metrics["counter_initial"] = {key: samples[0][key] for key in COUNTERS if key in samples[0]}
        metrics["counter_final"] = {key: samples[-1][key] for key in COUNTERS if key in samples[-1]}
        # Point samples are diagnostic evidence, not a between-sample minimum.
        for name in ("free_heap", "largest_internal"):
            if all(name in s for s in samples):
                window = min(60, len(samples))
                first = sum(s[name] for s in samples[:window]) / window
                last = sum(s[name] for s in samples[-window:]) / window
                metrics[name + "_sampled_min"] = min(s[name] for s in samples)
                metrics[name + "_first_window_mean"] = round(first, 1)
                metrics[name + "_last_window_mean"] = round(last, 1)
                metrics[name + "_window_change"] = round(last - first, 1)
    checks["errors"] = complete and all(s["errors"] == samples[0]["errors"] for s in samples)
    checks["allocation_reporting"] = bool(samples) and all(s.get("alloc_hook") == 1 for s in samples)
    checks["allocation_failures"] = bool(samples) and all(s.get("alloc_fail") == 0 for s in samples)
    if samples and all("alloc_fail" in s for s in samples):
        metrics["reported_heap_allocation_failure_seen"] = max(s["alloc_fail"] for s in samples)
    # No raw serial lines, arbitrary fields, credentials, or capture records.
    observation_keys = ("result", "strict", "elapsed_seconds", "valid_status", "malformed_status",
                        "oversize_lines", "other_lines_omitted", "max_status_gap_seconds",
                        "first_status_seconds", "last_status_seconds")
    return {"target": TARGET, "result": "passed" if all(checks.values()) else "failed",
            "seconds": seconds, "expected_mode": expected_mode, "checks": checks, "metrics": metrics,
            "observation": {key: observation[key] for key in observation_keys if key in observation},
            "identity": {"reported_version": samples[0].get("version") if samples else None,
                         "operator_expected_app_sha256": app_sha256(expected_app_sha256) if expected_app_sha256 is not None else None,
                         "device_app_sha256_verified": False,
                         "binding": "Optional hash supplied by the operator; no device image readback is performed."},
            "measurement_scopes": {
                "min_internal": "Allocator lifetime low-water mark since boot; not reset for this run. A prior settings/save/upload dip still fails this strict check.",
                "free_heap_sampled_min": "Smallest observed point sample; cannot rule out a lower value between polls.",
                "task_stack": "FreeRTOS lifetime high-water margins in bytes for the loop and writer tasks, not every task or only this interval.",
                "ui": "Dispatch-to-presentation frame samples: p95 is the last 64 frames and max is since UI initialization. Sampled rolling p95 is not a whole-run percentile; multiple keys may coalesce into one frame.",
                "allocation_failures": "Latched public heap allocator failure since setup registration; excludes earlier startup, direct internal allocation and private pool exhaustion.",
                "errors": "No increase or decrease relative to the first sample; pre-existing errors are retained explicitly as the baseline.",
            },
            "samples": samples,
            "scope": "Runtime metrics only; not RF loss, SD readback, GPS accuracy or upload acceptance. UI timing starts at key dispatch, excluding keyboard hardware and FIFO delay; measure physical input-to-draw separately."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=1800)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expected-mode", choices=("wardriver", "recon"), default="wardriver")
    parser.add_argument("--expected-app-sha256", type=app_sha256,
                        help="optional operator-supplied image hash; not verified from the device")
    args = parser.parse_args(argv)
    if not 1 <= args.seconds <= 3600:
        parser.error("seconds must be 1-3600; acceptance requires at least 1800")
    if args.output.exists():
        parser.error("Choose a new output file")
    samples, observation = [], {}
    def receive(status, elapsed):
        status["elapsed_seconds"] = elapsed
        samples.append(status)
        if len(samples) % 30 == 0:
            print(f"Collected {len(samples)} status samples", flush=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
    connection.dtr = connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        with connection:
            result = run_bench(connection, "status", args.seconds, strict=True,
                               observation=observation, on_status=receive)
    except (OSError, serial.SerialException, KeyboardInterrupt):
        result = 1
    report = evaluate(samples, result, args.seconds, observation=observation,
                      expected_app_sha256=args.expected_app_sha256, expected_mode=args.expected_mode)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    # Full public sample series lives in the artifact; console stays bounded.
    print(json.dumps({key: value for key, value in report.items() if key != "samples"}, indent=2))
    return 0 if report["result"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
