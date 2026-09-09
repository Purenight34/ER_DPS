"""Versioned official API snapshots. Explicit invocation only; never run by tests."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any

from game_data_api import EternalReturnApiError, TABLE_NAME, get_game_data, get_korean_localization

REQUIRED_TABLES = (
    "Character", "CharacterLevelUpStat", "CharacterMastery", "WeaponTypeInfo",
    "ItemWeapon", "ItemArmor",
)
OPTIONAL_TABLES = (
    "CharacterModeModifier", "ItemSkillLinker", "Skill", "SkillGroup",
    "MasteryLevel", "MasteryStat", "CharacterSkill", "Potential", "PotentialGroup", "PotentialSlot",
    "Trait", "CharacterAttributes", "Level",
)
SOURCE_DOCUMENT = "https://developer.eternalreturn.io/static/media/OpenAPI_KR_20260724.html"
SCHEMA_VERSION = 1


class SyncError(RuntimeError):
    pass


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def _sha(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def _atomic_write(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=".sync-", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _validate_hashes(value: Any) -> dict[str, str | int]:
    if not isinstance(value, dict) or not value:
        raise SyncError("Official hash table is empty or invalid.")
    for key, token in value.items():
        if (not isinstance(key, str) or not TABLE_NAME.fullmatch(key)
                or isinstance(token, bool) or not isinstance(token, (str, int))):
            raise SyncError("Official hash table contains an invalid entry.")
    return value


def _load_previous(root: Path) -> tuple[Path | None, dict[str, Any]]:
    try:
        pointer = json.loads((root / "current.json").read_text(encoding="utf-8"))
        relative = pointer["snapshot"]
        if not isinstance(relative, str) or not re.fullmatch(r"snapshots/api-[0-9a-f]{24}", relative):
            return None, {}
        snapshot = root / relative
        content = (snapshot / "manifest.json").read_bytes()
        if _sha(content) != pointer["manifest_sha256"]:
            return None, {}
        manifest = json.loads(content)
        if not isinstance(manifest, dict) or manifest.get("schema_version") != SCHEMA_VERSION:
            return None, {}
        return snapshot, manifest
    except (OSError, ValueError, KeyError, TypeError):
        return None, {}


def _read_verified(snapshot: Path | None, filename: str, sha256: Any) -> bytes | None:
    if snapshot is None or not isinstance(sha256, str):
        return None
    try:
        content = (snapshot / filename).read_bytes()
        return content if _sha(content) == sha256 else None
    except OSError:
        return None


def sync_version(data_root: Path, tables: list[str] | None = None) -> dict[str, Any]:
    """Fetch metadata, reuse verified unchanged files, publish only a complete snapshot."""
    root = data_root.resolve()
    hashes = _validate_hashes(get_game_data("hash"))
    if any(name not in hashes for name in REQUIRED_TABLES):
        raise SyncError("Official API is missing a required character/equipment table.")
    requested = list(dict.fromkeys((*REQUIRED_TABLES, *(name for name in OPTIONAL_TABLES if name in hashes), *(tables or []))))
    if any(name not in hashes for name in requested):
        raise SyncError("Requested extra table is not listed by the official hash endpoint.")
    source_hash = _sha(_json_bytes(hashes))
    previous_path, previous = _load_previous(root)
    previous_tables = previous.get("tables", {})
    if not isinstance(previous_tables, dict):
        previous_tables = {}
    files: dict[str, bytes] = {}
    entries: dict[str, Any] = {}
    fetched: list[str] = []
    reused: list[str] = []
    for table in requested:
        prior = previous_tables.get(table, {})
        if not isinstance(prior, dict):
            prior = {}
        content = None
        if prior.get("source_hash") == hashes[table]:
            content = _read_verified(previous_path, f"{table}.json", prior.get("content_sha256"))
        if content is None:
            rows = get_game_data(table)
            if not isinstance(rows, list) or not all(isinstance(row, dict) for row in rows):
                raise SyncError(f"Official {table} table is not a list of rows.")
            try:
                content = _json_bytes(rows)
            except (TypeError, ValueError):
                raise SyncError(f"Official {table} table contains invalid JSON values.") from None
            fetched.append(table)
        else:
            reused.append(table)
        files[f"{table}.json"] = content
        entries[table] = {"source_hash": hashes[table], "content_sha256": _sha(content)}
    localization = None
    previous_localization = previous.get("localization", {})
    if previous.get("source_hash_sha256") == source_hash and isinstance(previous_localization, dict):
        localization = _read_verified(previous_path, "Korean.txt", previous_localization.get("content_sha256"))
    if localization is None:
        localization = get_korean_localization().encode("utf-8")
    files["Korean.txt"] = localization
    identity = {"source_hash_sha256": source_hash, "tables": entries, "korean_sha256": _sha(localization)}
    version = "api-" + _sha(_json_bytes(identity))[:24]
    snapshot = root / "snapshots" / version
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "data_version": version,
        "retrieved_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_document": SOURCE_DOCUMENT,
        "source_hash_sha256": source_hash,
        "source_table_hashes": hashes,
        "game_patch_version": "not-provided-by-api",
        "tables": entries,
        "localization": {"language": "Korean", "content_sha256": _sha(localization)},
    }
    # Preserve the original retrieval timestamp and manifest if this exact snapshot is intact.
    if previous_path == snapshot and all(_read_verified(snapshot, name, _sha(content)) is not None for name, content in files.items()):
        return {"data_version": version, "snapshot": str(snapshot), "fetched": fetched, "reused": reused, "unchanged": True}
    # Do not attribute rows fetched across an API revision change to one snapshot.
    if _validate_hashes(get_game_data("hash")) != hashes:
        raise SyncError("Official table hashes changed during sync; current snapshot was preserved. Retry the update.")
    # Every network/schema operation above completes before publication starts.
    snapshot.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        _atomic_write(snapshot / name, content)
    manifest_content = _json_bytes(manifest)
    _atomic_write(snapshot / "manifest.json", manifest_content)
    _atomic_write(root / "current.json", _json_bytes({
        "schema_version": SCHEMA_VERSION,
        "data_version": version,
        "snapshot": f"snapshots/{version}",
        "manifest_sha256": _sha(manifest_content),
    }))
    return {"data_version": version, "snapshot": str(snapshot), "fetched": fetched, "reused": reused, "unchanged": False}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Explicit read-only official game data snapshot update.")
    parser.add_argument("--data-dir", type=Path, default=Path(__file__).resolve().parent / "data")
    parser.add_argument("--tables", nargs="*", help="Extra table names; must be present in official hash metadata.")
    parser.add_argument("--raw-only", action="store_true", help="Save raw snapshot only (currently the default).")
    args = parser.parse_args(argv)
    try:
        result = sync_version(args.data_dir, args.tables)
    except (EternalReturnApiError, SyncError) as error:
        print(str(error), file=sys.stderr)
        return 1
    except OSError:
        print("Snapshot filesystem operation failed; current pointer was not intentionally advanced.", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
