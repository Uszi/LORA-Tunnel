#!/usr/bin/env python3
"""Read only allowlisted runtime metrics during a manually started capture."""
import argparse
import json
from pathlib import Path
import sys

import serial
from bench_serial import parse_status, run_bench

TARGET = "cardputer-adv-lora1262"


def evaluate(samples, serial_result, seconds):
    checks = {"serial": serial_result == 0, "duration": seconds >= 1800,
              "samples": len(samples) >= int(seconds * 0.9)}
    checks["target"] = bool(samples) and all(s.get("target") == TARGET for s in samples)
    checks["capture"] = bool(samples) and all(s.get("ready") == 1 and s.get("running") == 1
        and s.get("saving") == 0 and s.get("sd") == 1 and s.get("upload") == 0 for s in samples)
    limits = {"min_internal": 32768, "largest_internal": 16384,
              "loop_stack": 1024, "writer_stack": 1024}
    for name, limit in limits.items():
        checks[name] = bool(samples) and all(s.get(name, -1) >= limit for s in samples)
    checks["ui_samples"] = bool(samples) and samples[-1].get("ui_samples", 0) - samples[0].get("ui_samples", 0) >= 30
    checks["ui_p95"] = bool(samples) and all(0 <= s.get("ui_p95_ms", -1) <= 100 for s in samples)
    checks["ui_max"] = bool(samples) and all(0 <= s.get("ui_max_ms", -1) <= 250 for s in samples)
    metrics = {key: min(s[key] for s in samples) for key in limits
               if samples and all(key in s for s in samples)}
    if samples:
        metrics.update(drop_delta=samples[-1]["drops"] - samples[0]["drops"],
                       error_delta=samples[-1]["errors"] - samples[0]["errors"],
                       gps_fix_samples=sum(s["gps_fix"] for s in samples),
                       lora_rx_delta=samples[-1]["lora_rx"] - samples[0]["lora_rx"])
        # Preserve trend evidence for the operator; allocator fluctuations are
        # not automatically classified as a leak or proof of leak-free capture.
        for name in ("free_heap", "largest_internal"):
            if all(name in s for s in samples):
                window=min(60,len(samples))
                first=sum(s[name] for s in samples[:window])/window
                last=sum(s[name] for s in samples[-window:])/window
                metrics[name+"_first_window_mean"]=round(first,1)
                metrics[name+"_last_window_mean"]=round(last,1)
                metrics[name+"_window_change"]=round(last-first,1)
    checks["errors"] = metrics.get("error_delta") == 0
    return {"target": TARGET, "result": "passed" if all(checks.values()) else "failed",
            "seconds": seconds, "checks": checks, "metrics": metrics,
            "scope": "Runtime metrics only; not RF loss, SD readback, GPS accuracy or upload acceptance. UI timing starts at key dispatch, excluding keyboard hardware and FIFO delay; measure physical input-to-draw separately."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=1800)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("seconds must be 1–3600; acceptance requires at least1800")
    if args.output.exists():
        parser.error("Choose a new output file")
    samples = []
    def receive(line):
        status = parse_status(line)
        if status is not None:
            samples.append(status)
            if len(samples) % 30 == 0:
                print(f"Collected {len(samples)} status samples", flush=True)
        else:
            print(line, flush=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
    connection.dtr = connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        with connection:
            result = run_bench(connection, "status", args.seconds, emit=receive)
    except (OSError, serial.SerialException):
        result = 1
    report = evaluate(samples, result, args.seconds)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["result"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
