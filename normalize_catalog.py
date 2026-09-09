"""Convert versioned official data to a UTF-8 C++ catalog; never calculate stats."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path


TABLES = (
    "Character", "CharacterLevelUpStat", "CharacterMastery", "CharacterModeModifier",
    "WeaponTypeInfo", "ItemWeapon", "ItemArmor", "MasteryLevel", "MasteryStat", "Trait",
)


def escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def scalar(value) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("Nonfinite game data")
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def parse_localization(text: str) -> dict[str, str]:
    result = {}
    for line in text.lstrip("\ufeff").splitlines():
        if "┃" not in line:
            continue
        key, value = line.split("┃", 1)
        if key in result and result[key] != value:
            raise ValueError(f"Duplicate localization key: {key}")
        result[key] = value
    return result


def display_name(table: str, row: dict, strings: dict[str, str]) -> str:
    prefix = "Item" if table in ("ItemWeapon", "ItemArmor") else table
    code = row.get("code", "")
    key = f"{prefix}/Name/{code}"
    if key in strings:
        return strings[key]
    return str(row.get("name", code)) + " [번역 없음]"


def description(table: str, row: dict, strings: dict[str, str]) -> str:
    prefix = "Item" if table in ("ItemWeapon", "ItemArmor") else table
    code = row.get("code", "")
    if prefix == "Item":
        return strings.get(f"Item/Help/{code}", "")
    if table == "Trait":
        return strings.get(f"Trait/Tooltip/{code}", "")
    return strings.get(f"{prefix}/Desc/{code}", strings.get(f"{prefix}/Description/{code}", ""))


def normalize(snapshot: Path, output: Path, patch_version: str = "API 미제공") -> int:
    manifest_path = snapshot / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError("A completed snapshot manifest is required")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or manifest.get("data_version") != snapshot.name:
        raise ValueError("Snapshot manifest version does not match its directory")
    source_paths = [manifest_path, snapshot / "Korean.txt", *(snapshot / f"{table}.json" for table in TABLES)]
    if output.resolve() in (path.resolve() for path in source_paths):
        raise ValueError("Catalog output must not overwrite a source file")

    def verified(path: Path, expected: str) -> bytes:
        content = path.read_bytes()
        if not isinstance(expected, str) or hashlib.sha256(content).hexdigest() != expected:
            raise ValueError(f"Snapshot checksum mismatch: {path.name}")
        return content

    strings = parse_localization(verified(snapshot / "Korean.txt", manifest.get("localization", {}).get("content_sha256")).decode("utf-8-sig"))
    lines = ["# er-catalog-v1"]

    def emit(table, row_id, field, value):
        lines.append("\t".join(escape(str(x)) for x in (table, row_id, field, value)))

    emit("Meta", "0", "data_version", snapshot.name)
    emit("Meta", "0", "patch_version", patch_version)
    emit("Meta", "0", "source", "https://open-api.bser.io/v2/data/{metaType}; /v1/l10n/Korean")
    emit("Meta", "0", "manifest", json.dumps(manifest, ensure_ascii=False, separators=(",", ":")))
    record_count = 0
    for table in TABLES:
        path = snapshot / f"{table}.json"
        if not path.is_file():
            if table in manifest.get("tables", {}):
                raise ValueError(f"Missing manifested table: {table}")
            continue
        rows = json.loads(verified(path, manifest.get("tables", {}).get(table, {}).get("content_sha256")))
        if isinstance(rows, dict) and "data" in rows:
            rows = rows["data"]
        if not isinstance(rows, list):
            raise ValueError(f"Expected records in {table}")
        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                raise ValueError(f"Expected record in {table}")
            for field, value in row.items():
                if field.startswith("_"):
                    raise ValueError("Reserved catalog field in source")
                emit(table, index, field, scalar(value))
            emit(table, index, "_name", display_name(table, row, strings))
            emit(table, index, "_description", description(table, row, strings))
            record_count += 1
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(output)
    return record_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("data/catalog.tsv"))
    parser.add_argument("--patch-version", default="API 미제공")
    args = parser.parse_args()
    count = normalize(args.snapshot, args.output, args.patch_version)
    print(f"Catalog saved: {count} records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
