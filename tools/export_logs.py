#!/usr/bin/env python3
"""Offline, streaming exports for LORA Tunnel schema 1 JSONL. Never uploads."""
from __future__ import annotations

import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import sys
from typing import Any, Iterator, TextIO

MAX_LINE_BYTES = 65536
WIGLE_COLUMNS = (
    "MAC", "SSID", "AuthMode", "FirstSeen", "Channel", "Frequency", "RSSI",
    "CurrentLatitude", "CurrentLongitude", "AltitudeMeters", "AccuracyMeters",
    "RCOIs", "MfgrId", "Type",
)
MAC_RE = re.compile(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\Z")
MESH_ROLES = {
    "meshtastic": {"CLIENT", "CLIENT_MUTE", "ROUTER", "REPEATER", "ROUTER_CLIENT",
                   "TRACKER", "SENSOR", "TAK", "CLIENT_HIDDEN", "LOST_AND_FOUND",
                   "TAK_TRACKER", "ROUTER_LATE", "CLIENT_BASE"},
    "meshcore": {"Repeater", "Client", "Room Server", "RoomServer", "Sensor"},
}


def number(value: Any, low: float, high: float) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and low <= value <= high and math.isfinite(value))


def integer(value: Any, low: int, high: int) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def utc_text(value: Any) -> str | None:
    """Require an explicitly timezone-aware timestamp; normalize to UTC."""
    if not isinstance(value, str) or len(value) > 40:
        return None
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if stamp.tzinfo is None or stamp.utcoffset() is None or stamp.year < 2020:
            return None
        return stamp.astimezone(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    except (ValueError, OverflowError):
        return None


def records(path: Path, counts: Counter) -> Iterator[dict]:
    """Bound memory per input record and skip corrupt/interrupted records visibly."""
    with path.open("rb") as source:
        while True:
            raw = source.readline(MAX_LINE_BYTES + 1)
            if not raw:
                return
            counts["input_lines"] += 1
            if len(raw) > MAX_LINE_BYTES:
                while raw and not raw.endswith(b"\n"):
                    raw = source.readline(MAX_LINE_BYTES + 1)
                counts["skipped_oversize"] += 1
                continue
            if not raw.strip():
                counts["skipped_blank"] += 1
                continue
            try:
                item = json.loads(raw.decode("utf-8"), parse_constant=lambda _: None)
            except (ValueError, UnicodeDecodeError, RecursionError):
                counts["skipped_malformed_json"] += 1
                continue
            if not isinstance(item, dict) or type(item.get("schema")) is not int or item["schema"] != 1:
                counts["skipped_schema"] += 1
                continue
            yield item


def text_field(value: Any, limit: int = 512) -> str | None:
    if value is None:
        return ""
    if not isinstance(value, str) or len(value) > limit or "\x00" in value:
        return None
    # WiGLE rows are line oriented. Preserve original bytes/content in JSONL;
    # escape control characters only in the interchange representation.
    return value.replace("\r", "\\r").replace("\n", "\\n")


def wigle_row(event: dict, counts: Counter, max_fix_age_ms: int,
              include_random_ble: bool) -> list | None:
    # Missing mode is a schema-1 legacy wardrive record. New/unknown modes
    # must not silently become AP/device inventory uploads.
    if event.get("capture_mode", "wardriver") != "wardriver":
        counts["skipped_nonwardriver_mode"] += 1
        return None
    kind = event.get("type")
    if kind not in ("wifi", "ble"):
        counts["skipped_not_wifi_ble"] += 1
        return None
    radio = event.get(kind)
    if (kind == "wifi" and (event.get("wifi_frame", False) is not False
            or (isinstance(radio, dict) and (radio.get("wifi_frame", False) is not False
                                            or "raw_frame_hex" in radio)))):
        # A transmitter from a probe/action/NAN frame is not an observed AP.
        counts["skipped_raw_wifi_frame"] += 1
        return None
    gps = event.get("gps")
    if (not isinstance(gps, dict) or gps.get("valid") is not True
            or not number(gps.get("age_ms"), 0, max_fix_age_ms)
            or not number(gps.get("lat"), -90, 90)
            or not number(gps.get("lon"), -180, 180)):
        counts["skipped_missing_stale_invalid_receiver_fix"] += 1
        return None
    stamp = utc_text(event.get("utc"))
    if stamp is None:
        counts["skipped_invalid_utc"] += 1
        return None
    if not isinstance(radio, dict):
        counts["skipped_malformed_radio"] += 1
        return None
    address = radio.get("bssid" if kind == "wifi" else "address")
    name = text_field(radio.get("ssid" if kind == "wifi" else "name"))
    if (not isinstance(address, str) or not MAC_RE.fullmatch(address)
            or name is None or not number(radio.get("rssi"), -127, 20)):
        counts["skipped_malformed_radio"] += 1
        return None
    channel, frequency, mfgr = "", "", ""
    if kind == "wifi":
        channel = radio.get("channel")
        frequency = radio.get("frequency_mhz")
        expected = 2484 if channel == 14 else (2407 + 5 * channel if integer(channel, 1, 13) else None)
        security = text_field(radio.get("security"), 256)
        if expected is None or frequency != expected or security is None:
            counts["skipped_invalid_wifi_settings"] += 1
            return None
        if radio.get("ssid_sanitized") is True:
            # A display-safe '?' is not the observed SSID. Recover only exact,
            # valid UTF-8 from the SDK-returned bytes, otherwise leave it unknown.
            original = radio.get("ssid_hex")
            recovered = None
            if isinstance(original, str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,32}", original):
                try:
                    recovered = text_field(bytes.fromhex(original).decode("utf-8"))
                except UnicodeDecodeError:
                    pass
            name = recovered if recovered is not None else ""
            counts["ssid_recovered_from_raw" if recovered is not None else "ssid_blank_invalid_encoding"] += 1
    else:
        address_type = radio.get("address_type")
        if address_type not in ("public", "public_identity"):
            if not include_random_ble:
                counts["skipped_nonpublic_ble"] += 1
                return None
            if address_type not in ("random", "random_static", "random_resolvable",
                                    "random_nonresolvable", "random_identity"):
                counts["skipped_unknown_ble_address_type"] += 1
                return None
            counts["included_random_ble"] += 1
        security = "[BLE]"
        if "manufacturer_id" in radio and radio["manufacturer_id"] is not None:
            if not integer(radio["manufacturer_id"], 0, 65535):
                counts["skipped_invalid_manufacturer_id"] += 1
                return None
            mfgr = radio["manufacturer_id"]
        if radio.get("name_sanitized") is True:
            # The native payload remains available for a future AD decoder.
            name = ""
            counts["ble_name_blank_invalid_encoding"] += 1
    altitude = gps.get("alt_m")
    accuracy = gps.get("accuracy_m")
    # HDOP has no metre unit and must never masquerade as AccuracyMeters.
    altitude = altitude if number(altitude, -1000, 100000) else ""
    accuracy = accuracy if number(accuracy, 0, 100000) else ""
    return [address.upper(), name, security, stamp, channel, frequency, radio["rssi"],
            gps["lat"], gps["lon"], altitude, accuracy, "", mfgr, kind.upper()]


def export_wigle(source: Path, destination: TextIO, counts: Counter,
                 max_fix_age_ms: int = 10000, include_random_ble: bool = False) -> None:
    writer = csv.writer(destination, lineterminator="\n")
    writer.writerow(["WigleWifi-1.6", "appRelease=LORA-Tunnel-0.1.0", "model=Heltec-V4-R8",
                     "release=0.1.0", "device=LORA-Tunnel", "display=touchscreen",
                     "board=Heltec-Expansion-V2", "brand=Valleytech"])
    writer.writerow(WIGLE_COLUMNS)
    for event in records(source, counts):
        row = wigle_row(event, counts, max_fix_age_ms, include_random_ble)
        if row is not None:
            writer.writerow(row)
            counts["exported"] += 1


def mesh_record(event: dict, counts: Counter) -> dict | None:
    if event.get("capture_mode", "wardriver") != "wardriver":
        counts["skipped_nonwardriver_mode"] += 1
        return None
    if event.get("type") != "lora" or not isinstance(event.get("mesh"), dict):
        counts["skipped_not_decoded_mesh"] += 1
        return None
    mesh = event["mesh"]
    protocol = mesh.get("protocol")
    position = mesh.get("node_position")
    stamp = utc_text(event.get("utc"))
    if not isinstance(protocol, str) or protocol not in MESH_ROLES or mesh.get("decode_status") != "decoded":
        counts["skipped_not_decoded_mesh"] += 1
        return None
    if (not isinstance(position, dict) or position.get("valid") is not True
            or position.get("source") != "node_advertised"
            or not number(position.get("lat"), -90, 90)
            or not number(position.get("lon"), -180, 180)
            or (position["lat"] == 0 and position["lon"] == 0)):
        counts["skipped_missing_invalid_node_position"] += 1
        return None
    node_id = mesh.get("source_id")
    if not isinstance(node_id, str):
        counts["skipped_invalid_mesh_id"] += 1
        return None
    public_key = mesh.get("public_key")
    if protocol == "meshtastic":
        if not re.fullmatch(r"[0-9a-fA-F]{8}", node_id) or node_id.lower() in ("00000000", "ffffffff"):
            counts["skipped_invalid_mesh_id"] += 1
            return None
        node_id = node_id.lower()
    else:
        if not isinstance(public_key, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", public_key):
            counts["skipped_missing_meshcore_public_key"] += 1
            return None
        public_key = public_key.lower()
        node_id = public_key[:16]
        if mesh["source_id"].lower() != node_id:
            counts["skipped_meshcore_key_id_mismatch"] += 1
            return None
    if not isinstance(mesh.get("node_type"), str) or mesh["node_type"] not in MESH_ROLES[protocol] or stamp is None:
        counts["skipped_missing_mesh_role_or_utc"] += 1
        return None
    # Missing hops means "direct" on this service. Do not make that assertion
    # for packets whose forwarding history is unavailable.
    hops = mesh.get("hop_count")
    if not integer(hops, 0, 255):
        counts["skipped_unknown_mesh_hops"] += 1
        return None
    name = text_field(mesh.get("name"), 128)
    lora = event.get("lora")
    rssi = lora.get("rssi") if isinstance(lora, dict) else None
    if name is None or not number(rssi, -200, 20):
        counts["skipped_malformed_mesh_metadata"] += 1
        return None
    record = {"node_id": node_id, "node_type": mesh["node_type"], "name": name or node_id,
              "lat": position["lat"], "lon": position["lon"], "rssi": rssi,
              "first_seen": stamp, "type": "MESHCORE", "network": protocol,
              "path_hops": hops}
    if protocol == "meshcore":
        record["public_key"] = public_key
    return record


def export_mesh(source: Path, destination: TextIO, counts: Counter) -> None:
    """Write the unsigned payload shape incrementally, not an HTTP request."""
    destination.write('{"networks":[],"aircraft":[],"meshcore_nodes":[')
    first = True
    for event in records(source, counts):
        record = mesh_record(event, counts)
        if record is not None:
            if not first:
                destination.write(",")
            json.dump(record, destination, ensure_ascii=True, separators=(",", ":"), allow_nan=False)
            first = False
            counts["exported"] += 1
    destination.write("]}\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("format", choices=("wigle", "wdgwars-mesh-candidate"))
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-fix-age-ms", type=int, default=10000)
    parser.add_argument("--include-random-ble", action="store_true",
                        help="Include known random BLE addresses; CSV cannot retain address type")
    args = parser.parse_args(argv)
    if args.input.resolve() == args.output.resolve():
        parser.error("output must differ from the original local log")
    if not 0 <= args.max_fix_age_ms <= 60000:
        parser.error("max-fix-age-ms must be between 0 and 60000")
    counts: Counter = Counter()
    try:
        with args.output.open("x", encoding="utf-8", newline="") as destination:
            if args.format == "wigle":
                export_wigle(args.input, destination, counts, args.max_fix_age_ms, args.include_random_ble)
            else:
                export_mesh(args.input, destination, counts)
    except (OSError, ValueError) as exc:
        print(f"Export failed: {exc}", file=sys.stderr)
        return 1
    report = {"format": args.format, "counts": dict(counts), "uploaded": False}
    if args.format == "wdgwars-mesh-candidate":
        report["status"] = "candidate_payload_unverified_by_service"
    print(json.dumps(report, sort_keys=True), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
