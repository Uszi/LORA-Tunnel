"""Validate the public JSON schemas and shipped configuration/log examples."""
import json
from pathlib import Path
from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]


def main():
    validators = {}
    for path in sorted((ROOT / "schemas").glob("*.schema.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        validators[path.name] = Draft202012Validator(schema)
    count = 0
    for relative, schema in (
        ("sdcard/lora-tunnel/config.example.json", "config-v1.schema.json"),
        ("sdcard/lora-tunnel/detection_rules.json", "detection-rules-v1.schema.json"),
        ("sdcard/lora-tunnel/detection_rules.example.json", "detection-rules-v1.schema.json"),
    ):
        validators[schema].validate(json.loads((ROOT / relative).read_text(encoding="utf-8")))
        count += 1
    for number, line in enumerate((ROOT / "examples/synthetic-session.jsonl").read_text(encoding="utf-8").splitlines(), 1):
        if line.strip():
            try:
                validators["observation-v1.schema.json"].validate(json.loads(line))
            except Exception as error:
                raise ValueError(f"synthetic-session.jsonl:{number}: {error}") from error
            count += 1
    print(f"Validated {len(validators)} schemas and {count} example documents/records")


if __name__ == "__main__":
    main()
