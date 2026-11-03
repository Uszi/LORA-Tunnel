"""Invalidate Cardputer objects when its forced radio configuration changes.

PlatformIO clones this pre-script environment into framework/library builders.
Putting the full content digest in CPPDEFINES updates their command signatures,
including third-party objects whose dependency scan misses forced includes.
"""
import hashlib
from pathlib import Path

Import("env")

if env["PIOENV"] == "cardputer-adv-lora1262":
    header = Path(env["PROJECT_DIR"]) / "include/cardputer_nimble_config.h"
    digest = hashlib.sha256(header.read_bytes()).hexdigest()
    env.Append(CPPDEFINES=[("LORA_TUNNEL_CARDPUTER_CONFIG_SHA256", '\\"' + digest + '\\"')])
