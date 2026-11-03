"""Generate and verify Cardputer-only non-ISR placement from pinned IDF 5.5.5 rules.

The precompiled SDK is read-only. All matching is archive + object + exact input
section; compiler clones are recognized explicitly, never by a function prefix.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "linker/cardputer/esp-idf-v5.5.5"
ARCHIVES = {"libfreertos.a": "qio_qspi/libfreertos.a", "libesp_ringbuf.a": "lib/libesp_ringbuf.a"}
CRITICAL = {"xTaskGetSchedulerState", "xTaskGetTickCount", "xTaskIncrementTick", "xTaskIncrementTickOtherCores", "vTaskSwitchContext", "prvSelectHighestPriorityTaskSMP", "prvCopyDataToQueue", "prvCopyDataFromQueue", "prvNotifyQueueSetContainer", "xTaskRemoveFromEventList", "prvResetNextTaskUnblockTime", "prvIsYieldRequiredSMP", "prvCheckTaskCanBeScheduledSMP"}
CLONE = re.compile(r"(?:[.$](?:constprop|isra|part|cold)[.$][0-9]+)+$")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(ok: bool, message: str):
    if not ok:
        raise RuntimeError("Cardputer linker: " + message)


def write_changed(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def read_config(data: str) -> dict[str, str]:
    return dict(re.findall(r"^CONFIG_(\w+)=(.*)$", data, re.M))


def condition(expression: str, config: dict[str, str]) -> bool:
    expression = expression.strip().strip("()")
    if "&&" in expression:
        return all(condition(part, config) for part in expression.split("&&"))
    if "||" in expression:
        return any(condition(part, config) for part in expression.split("||"))
    match = re.fullmatch(r"(\w+)\s*(=|!=|>|<)\s*(y|n|\d+)", expression.strip())
    require(match is not None, "unsupported official condition: " + expression)
    key, operator, expected = match.groups()
    actual = config.get(key, "n")
    if operator in ("=", "!="):
        return (actual == expected) if operator == "=" else (actual != expected)
    number = 0 if actual == "n" else 1 if actual == "y" else int(actual, 0)
    return number > int(expected) if operator == ">" else number < int(expected)


def whitelist(source: str, config: dict[str, str]) -> set[tuple[str, str]]:
    active = []
    entries = set()
    for raw in source.splitlines():
        clean = raw.split("#", 1)[0].rstrip()
        if not clean.strip():
            continue
        indent = len(clean) - len(clean.lstrip())
        while active and active[-1][0] >= indent:
            active.pop()
        text = clean.strip()
        if text.startswith("if "):
            require(text.endswith(":"), "invalid conditional")
            active.append((indent, condition(text[3:-1], config) and all(v for _, v in active)))
            continue
        if not all(v for _, v in active):
            continue
        match = re.fullmatch(r"([\w*]+):\s*(\w+)\s*\(default\)", text)
        if match:
            obj, function = match.groups()
            require("FromISR" not in function and function not in CRITICAL,
                    "critical/ISR function in movable policy: " + function)
            entries.add((obj, function))
    require(bool(entries), "empty official whitelist")
    return entries


def object_base(name: str) -> str:
    return re.sub(r"(?:\.c)?\.(?:obj|o)$", "", name)


def section_function(section: str) -> str | None:
    match = re.fullmatch(r"\.(?:text|literal)\.(.+)", section)
    if not match:
        return None
    function = CLONE.sub("", match.group(1))
    return function if re.fullmatch(r"\w+", function) else None


def may_move(obj: str, section: str, allowed: set[tuple[str, str]]) -> bool:
    function = section_function(section)
    return function is not None and (object_base(obj), function) in allowed


def archive_sections(objdump: Path, archive: Path) -> list[dict]:
    output = subprocess.check_output([str(objdump), "-h", str(archive)], text=True)
    result, obj = [], None
    for line in output.splitlines():
        match = re.fullmatch(r"([^\s:]+):\s+file format elf32-xtensa-le", line)
        if match:
            obj = match.group(1)
        match = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+", line)
        if match:
            require(obj is not None, "archive section without object")
            name, size = match.group(1), int(match.group(2), 16)
            if size and (name.startswith(".text") or name.startswith(".literal") or name.startswith(".iram")):
                require(re.fullmatch(r"[A-Za-z0-9_.$]+", name) is not None, "unexpected section spelling")
                require(re.fullmatch(r"[A-Za-z0-9_.]+", obj) is not None, "unexpected object spelling")
                result.append({"object": obj, "section": name, "bytes": size})
    require(bool(result), "empty archive inventory")
    return result


def original_rules(script: str):
    lines = script.splitlines(keepends=True)
    ring = [i for i, line in enumerate(lines) if line.strip() == "*libesp_ringbuf.a:(.literal .literal.* .text .text.*)"]
    generic = [i for i, line in enumerate(lines) if line.startswith(" *libfreertos.a:(EXCLUDE_FILE(")]
    tasks = [i for i, line in enumerate(lines) if line.startswith(" *libfreertos.a:tasks.*(") and ".text.vTaskSwitchContext" in line]
    require(len(ring) == len(generic) == len(tasks) == 1, "original archive selection changed")
    excludes = set(re.findall(r"\*libfreertos\.a:([\w]+)\.\*", lines[generic[0]]))
    require(excludes == {"app_startup", "freertos_compatibility", "idf_additions", "idf_additions_event_groups", "tasks"}, "FreeRTOS object exceptions changed")
    task_sections = set(lines[tasks[0]].strip().split("(", 1)[1].rstrip(")").split())
    require(all(re.fullmatch(r"\.(?:text|literal)(?:\.[\w.$]+)?", item) for item in task_sections), "unexpected task section selector")
    return lines, ring[0], generic[0], tasks[0], excludes, task_sections


def selector(archive: str, obj: str, sections: list[str]) -> str:
    # Only the archive path is wildcarded. Object and function names are exact.
    return " *" + archive + ":" + obj + "(" + " ".join(sorted(sections)) + ")\n"


def generate(sdk: Path, objdump: Path, output: Path, policy: Path = POLICY) -> dict:
    manifest = json.loads((policy / "provenance.json").read_text())
    package = json.loads((sdk.parent / "package.json").read_text())
    require(package.get("version") == manifest["sdk_package_version"], "SDK package version drift")
    for relative, expected in manifest["sdk_inputs"].items():
        require(digest((sdk / relative).read_bytes()) == expected, "SDK input hash drift: " + relative)
    sources = {}
    for name, meta in manifest["sources"].items():
        data = (policy / name).read_bytes()
        require(digest(data) == meta["sha256"], "official whitelist hash drift: " + name)
        sources[name] = data.decode()
    config = read_config((sdk / "sdkconfig").read_text())
    require(config.get("FREERTOS_SMP", "n") == "n", "unsupported SMP FreeRTOS variant")
    config.update(manifest["policy"])
    require(config["FREERTOS_PLACE_ISR_FUNCTIONS_INTO_FLASH"] == config["RINGBUF_PLACE_ISR_FUNCTIONS_INTO_FLASH"] == "n", "ISR relocation forbidden")
    original = (sdk / "qio_qspi/sections.ld").read_text()
    lines, ring_index, generic_index, tasks_index, excludes, task_sections = original_rules(original)
    allowed = {"libfreertos.a": whitelist(sources["freertos.lf"], config), "libesp_ringbuf.a": whitelist(sources["esp_ringbuf.lf"], config)}
    moved, retained = [], []
    for archive, relative in ARCHIVES.items():
        inventory = archive_sections(objdump, sdk / relative)
        inventory_digest = digest(json.dumps(inventory, sort_keys=True, separators=(",", ":")).encode())
        require(inventory_digest == manifest["archive_inventory"][archive]["sha256"] and len(inventory) == manifest["archive_inventory"][archive]["sections"], "archive section inventory drift: " + archive)
        for item in inventory:
            section, obj = item["section"], item["object"]
            in_rule = not section.startswith(".iram") and (archive == "libesp_ringbuf.a" or object_base(obj) not in excludes or (object_base(obj) == "tasks" and section in task_sections))
            if not in_rule and not section.startswith(".iram"):
                continue  # Already in flash under the original SDK policy.
            row = {"archive": archive, **item, "explicit_rule": in_rule}
            (moved if in_rule and may_move(obj, section, allowed[archive]) else retained).append(row)
    require(moved and retained, "incomplete relocation inventory")
    require(any(section_function(row["section"]) == "xTaskGetTickCount" for row in retained), "cache-off tick function not retained")
    require(not any("FromISR" in row["section"] or section_function(row["section"]) in CRITICAL for row in moved), "critical relocation detected")
    def rules(rows, archive):
        grouped = {}
        for row in rows:
            if row["archive"] == archive and row["explicit_rule"]:
                grouped.setdefault(row["object"], []).append(row["section"])
        return "".join(selector(archive, obj, sections) for obj, sections in sorted(grouped.items()))
    lines[ring_index] = rules(retained, "libesp_ringbuf.a")
    lines[generic_index] = rules(retained, "libfreertos.a")
    lines[tasks_index] = ""  # Included in the exact retained inventory above.
    flash = [i for i, line in enumerate(lines) if line.strip() == "_text_start = ABSOLUTE(.);"]
    require(len(flash) == 1, "flash text insertion marker drift")
    lines[flash[0]] += " /* Project: official IDF 5.5.5 non-ISR flash policy only. */\n" + rules(moved, "libfreertos.a") + rules(moved, "libesp_ringbuf.a")
    generated = "".join(lines).encode()
    for row in moved + retained:
        row.pop("explicit_rule")
    moved.sort(key=lambda row: (row["archive"], row["object"], row["section"]))
    retained.sort(key=lambda row: (row["archive"], row["object"], row["section"]))
    report = {"schema": 1, "target": "cardputer-adv-lora1262", "policy": manifest["policy"], "sdk_inputs": manifest["sdk_inputs"], "official_sources": manifest["sources"], "generator_sha256": digest(Path(__file__).read_bytes()), "pio_hook_sha256": digest((ROOT / "tools/cardputer_linker_pio.py").read_bytes()), "provenance_sha256": digest((policy / "provenance.json").read_bytes()), "generated_sections_sha256": digest(generated), "moved": moved, "retained_iram": retained, "archive_movable_bytes": sum(row["bytes"] for row in moved)}
    write_changed(output / "sections.ld", generated)
    write_changed(output / "report.json", (json.dumps(report, indent=2, sort_keys=True) + "\n").encode())
    return report


def map_sections(text: str) -> dict[tuple[str, str, str], tuple[int, int]]:
    pattern = re.compile(r"^\s+(\.\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+[^\n]*?(libfreertos\.a|libesp_ringbuf\.a)\(([^)]+)\)", re.M)
    result = {}
    for match in pattern.finditer(text):
        section, address, size, archive, obj = match.groups()
        address, size = int(address, 16), int(size, 16)
        if address and size:
            key = archive, obj, section
            require(key not in result, "duplicate linked input section: " + str(key))
            result[key] = address, size
    return result


def verify(output: Path, map_path: Path, elf_path: Path) -> dict:
    report_bytes = (output / "report.json").read_bytes()
    report = json.loads(report_bytes)
    require(report["generated_sections_sha256"] == digest((output / "sections.ld").read_bytes()), "generated linker script drift")
    require(report["generator_sha256"] == digest(Path(__file__).read_bytes()), "generator changed after generation")
    require(report["pio_hook_sha256"] == digest((ROOT / "tools/cardputer_linker_pio.py").read_bytes()), "PIO hook changed after generation")
    require(report["provenance_sha256"] == digest((POLICY / "provenance.json").read_bytes()), "policy provenance changed after generation")
    records = map_sections(map_path.read_text())
    moved, retained = [], []
    for field, lower, upper, destination in [("moved", 0x42000000, 0x44000000, moved), ("retained_iram", 0x40300000, 0x40400000, retained)]:
        for row in report[field]:
            key = row["archive"], row["object"], row["section"]
            if key not in records:
                continue  # Link-time garbage collection is expected.
            address, size = records[key]
            require(lower <= address and address + size <= upper, "wrong placement: " + str(key) + " at " + hex(address))
            require(size <= row["bytes"], "unexpected linked input section growth")
            destination.append({**row, "address": hex(address), "linked_bytes": size})
    require(len(moved) >= 100 and len(retained) >= 40, "map lacks expected moved/retained section inventory")
    names = {section_function(row["section"]) for row in retained}
    require({"xTaskGetSchedulerState", "xTaskGetTickCount", "vTaskSwitchContext", "xTaskIncrementTick"} <= names, "map lacks critical IRAM functions")
    result = {"schema": 1, "target": report["target"], "report_sha256": digest(report_bytes), "generated_sections_sha256": report["generated_sections_sha256"], "map_sha256": digest(map_path.read_bytes()), "elf_sha256": digest(elf_path.read_bytes()), "moved_bytes": sum(row["linked_bytes"] for row in moved), "moved_sections": moved, "retained_iram_sections": retained}
    write_changed(output / "verification.json", (json.dumps(result, indent=2, sort_keys=True) + "\n").encode())
    return result


def replace_link_flags(flags: list, generated: Path) -> list:
    result, replacements = list(flags), 0
    for index, flag in enumerate(result):
        if str(flag) == "sections.ld":
            require(index > 0 and str(result[index - 1]) == "-T", "unexpected sections.ld flag format")
            result[index] = generated.as_posix()
            replacements += 1
    require(replacements == 1, "expected exactly one framework sections.ld argument")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("generate")
    create.add_argument("--sdk", type=Path, required=True)
    create.add_argument("--objdump", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    check = sub.add_parser("verify")
    check.add_argument("--output", type=Path, required=True)
    check.add_argument("--map", type=Path, required=True)
    check.add_argument("--elf", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "generate":
        report = generate(args.sdk, args.objdump, args.output)
        print("Cardputer linker generated:", len(report["moved"]), "eligible archive sections;", report["archive_movable_bytes"], "bytes before link GC")
    else:
        result = verify(args.output, args.map, args.elf)
        print("Cardputer linker verified:", len(result["moved_sections"]), "moved sections;", result["moved_bytes"], "bytes; critical IRAM retained")

if __name__ == "__main__":
    main()
