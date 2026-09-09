import importlib.util
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("normalize_catalog", ROOT / "normalize_catalog.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class NormalizeCatalogTests(unittest.TestCase):
    def test_korean_names_and_multiline_descriptions_are_data(self):
        strings = MODULE.parse_localization("Character/Name/11┃유키\nItem/Name/101┃검\nItem/Desc/101┃첫 줄\\n둘째 줄\n")
        self.assertEqual(strings["Character/Name/11"], "유키")
        self.assertEqual(MODULE.display_name("Character", {"code": 11, "name": "Yuki"}, strings), "유키")
        self.assertEqual(MODULE.display_name("ItemWeapon", {"code": 101, "name": "Sword"}, strings), "검")

    def test_tsv_escape_does_not_create_records(self):
        self.assertEqual(MODULE.escape("a\tb\nc\\d\r"), "a\\tb\\nc\\\\d\\r")

    def test_unknown_name_is_explicitly_untranslated(self):
        self.assertEqual(MODULE.display_name("Character", {"code": 11, "name": "Yuki"}, {}), "Yuki [번역 없음]")

    def test_no_calculation_or_unit_changes_in_raw_values(self):
        self.assertEqual(MODULE.scalar(0.022), "0.022")
        self.assertEqual(MODULE.scalar(0.75), "0.75")
        self.assertEqual(MODULE.scalar(0), "0")
        self.assertEqual(MODULE.scalar(False), "false")

    def test_duplicate_localization_key_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.parse_localization("Character/Name/11┃유키\nCharacter/Name/11┃다른 이름")

    def test_nonfinite_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.scalar(float("nan"))

    def test_official_tooltip_and_item_help_keys(self):
        strings = {"Item/Help/101": "검 설명", "Trait/Tooltip/201": "특성 설명"}
        self.assertEqual(MODULE.description("ItemWeapon", {"code": 101}, strings), "검 설명")
        self.assertEqual(MODULE.description("Trait", {"code": 201}, strings), "특성 설명")
        self.assertIn("Trait", MODULE.TABLES)

    def make_snapshot(self, root):
        snapshot = Path(root) / "api-test"
        snapshot.mkdir()
        korean = "Character/Name/11┃유키\nTrait/Name/201┃특성\nTrait/Tooltip/201┃설명\n".encode()
        (snapshot / "Korean.txt").write_bytes(korean)
        manifest = {"schema_version": 1, "data_version": "api-test", "tables": {},
                    "localization": {"content_sha256": hashlib.sha256(korean).hexdigest()}}
        for table, rows in (("Character", [{"code": 11, "attackPower": 36, "attackSpeed": 0.11}]),
                            ("Trait", [{"code": 201, "active": True}])):
            content = json.dumps(rows).encode()
            (snapshot / f"{table}.json").write_bytes(content)
            manifest["tables"][table] = {"content_sha256": hashlib.sha256(content).hexdigest()}
        (snapshot / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return snapshot

    def test_complete_catalog_preserves_names_values_and_provenance(self):
        with tempfile.TemporaryDirectory() as root:
            snapshot = self.make_snapshot(root)
            output = Path(root) / "catalog.tsv"
            self.assertEqual(MODULE.normalize(snapshot, output), 2)
            text = output.read_text(encoding="utf-8")
            self.assertIn("Character\t0\tattackSpeed\t0.11\n", text)
            self.assertIn("Trait\t0\t_name\t특성\n", text)
            self.assertIn("Trait\t0\t_description\t설명\n", text)
            self.assertIn("Meta\t0\tdata_version\tapi-test\n", text)

    def test_corrupt_snapshot_preserves_previous_catalog(self):
        with tempfile.TemporaryDirectory() as root:
            snapshot = self.make_snapshot(root)
            output = Path(root) / "catalog.tsv"
            output.write_text("previous", encoding="utf-8")
            (snapshot / "Character.json").write_text("[]", encoding="utf-8")
            with self.assertRaises(ValueError):
                MODULE.normalize(snapshot, output)
            self.assertEqual(output.read_text(), "previous")

    def test_localization_checksum_and_source_overwrite_are_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            snapshot = self.make_snapshot(root)
            with self.assertRaises(ValueError):
                MODULE.normalize(snapshot, snapshot / "Korean.txt")
            (snapshot / "Korean.txt").write_text("tampered", encoding="utf-8")
            with self.assertRaises(ValueError):
                MODULE.normalize(snapshot, Path(root) / "catalog.tsv")


if __name__ == "__main__":
    unittest.main()
