"""Offline end-to-end checks against the saved official catalog; no API/key access."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/zig/er_calc.exe"
CATALOG = ROOT / "data/catalog.tsv"


@unittest.skipUnless(EXE.is_file() and CATALOG.is_file(), "Build CLI and save data/catalog.tsv first")
class NamedCliIntegrationTests(unittest.TestCase):
    def invoke(self, *args, stdin=None, cwd=None):
        return subprocess.run(
            [str(EXE), *map(str, args)], input=stdin, capture_output=True,
            text=True, encoding="utf-8", errors="strict", cwd=cwd or ROOT, timeout=30,
        )

    def test_default_yuki_and_deterministic_json(self):
        args = ("--loadout", ROOT / "examples/yuki.ini", "--format", "json")
        first = self.invoke(*args)
        second = self.invoke(*args)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        parsed = json.loads(first.stdout)
        self.assertIn("유키", first.stdout)
        self.assertIn("녹슨 검", first.stdout)
        self.assertIsInstance(parsed, dict)
        self.assertNotIn("번역 없음", first.stdout)
        defender = parsed["scenario"]["defender"]
        self.assertEqual(defender["character_id"], "더미 [test-dummy]")
        for field in ("weapon_id", "equipment", "traits", "masteries", "skill_levels", "initial_effects"):
            self.assertEqual(defender[field], "none")
        self.assertEqual(parsed["result"]["defender"]["max_hp"]["total"], 1000)
        self.assertEqual(parsed["result"]["defender"]["defense"]["total"], 0)
        self.assertEqual(parsed["scenario"]["initial_hp"], 1000)
        self.assertEqual(parsed["scenario"]["initial_shield"], 0)
        self.assertEqual(parsed["result"]["total_damage"], parsed["result"]["impacts"][0]["raw_damage"])

    def test_korean_name_search(self):
        for kind, word in (("characters", "유키"), ("weapons", "녹슨 검"), ("traits", "취약")):
            with self.subTest(kind=kind):
                result = self.invoke("--list", kind, "--search", word)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(word, result.stdout)

    def test_default_interactive_works_outside_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.invoke(stdin="\n0\n", cwd=temporary)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("총 피해", result.stdout)
        self.assertNotIn("오류:", result.stdout)

    @unittest.skipUnless(os.name == "nt", "Windows launcher")
    def test_double_click_launcher_entrypoint(self):
        result = subprocess.run(
            ["cmd.exe", "/d", "/c", str(ROOT / "run_yuki.cmd")],
            input="\n0\n", capture_output=True, text=True, encoding="utf-8", timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("총 피해", result.stdout)
        self.assertNotIn("오류:", result.stdout)

    def test_interactive_named_selection_save_and_reload(self):
        with tempfile.TemporaryDirectory() as temporary:
            task = Path(temporary)
            (task / "data").mkdir()
            shutil.copyfile(CATALOG, task / "data/catalog.tsv")
            # Attacker -> weapon -> exact Korean name -> select; calculate/save/reset/load/calculate.
            result = self.invoke("--interactive", "--catalog", task / "data/catalog.tsv",
                                 stdin="2\n2\n장검\n1\n1\n9\n7\n8\n1\n0\n")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("오류:", result.stdout)
            saved = task / "outputs/yuki_test.ini"
            self.assertIn("weapon=장검", saved.read_text(encoding="utf-8"))
            expected = (task / "outputs/yuki_test.json").read_text(encoding="utf-8")
            replay = self.invoke("--loadout", saved, "--catalog", task / "data/catalog.tsv", "--format", "json")
            self.assertEqual(replay.returncode, 0, replay.stderr)
            self.assertEqual(replay.stdout, expected)

    def test_selected_trait_is_named_and_blocks_unimplemented_damage(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "trait.ini"
            config.write_text("[attacker]\ntrait=취약\n", encoding="utf-8")
            result = self.invoke("--loadout", config)
            self.assertEqual(result.returncode, 3, result.stderr)
            self.assertIn("취약", result.stdout)
            self.assertIn("피해 계산 불가", result.stdout)
            self.assertNotIn("총 피해", result.stdout)

    def test_dummy_settings_survive_save_reload_and_character_switch(self):
        with tempfile.TemporaryDirectory() as temporary:
            task = Path(temporary)
            (task / "data").mkdir()
            shutil.copyfile(CATALOG, task / "data/catalog.tsv")
            # Switch to character, then custom dummy, save, reset, reload, calculate.
            result = self.invoke("--interactive", "--catalog", task / "data/catalog.tsv",
                                 stdin="3\n2\n0\n3\n1\n1500\n100\n9\n7\n8\n1\n0\n")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("오류:", result.stdout)
            config = (task / "outputs/yuki_test.ini").read_text(encoding="utf-8")
            self.assertIn("[defender]\ntype=dummy\nmax_hp=1500\ndefense=100\n", config)
            saved = json.loads((task / "outputs/yuki_test.json").read_text(encoding="utf-8"))
            self.assertEqual(saved["scenario"]["initial_hp"], 1500)
            self.assertEqual(saved["result"]["defender"]["defense"]["total"], 100)
            self.assertEqual(saved["result"]["impacts"][0]["defense_multiplier"], 0.5)

    def test_legacy_character_defender_is_not_replaced_by_dummy(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "old.ini"
            config.write_text("[defender]\ncharacter=유키\nweapon=녹슨 검\nlevel=1\nweapon_mastery=1\n", encoding="utf-8")
            result = self.invoke("--loadout", config, "--format", "json")
            self.assertEqual(result.returncode, 0, result.stderr)
            saved = json.loads(result.stdout)
            self.assertEqual(saved["scenario"]["defender"]["character_id"], "유키 [11]")
            self.assertEqual(saved["result"]["defender"]["max_hp"]["total"], 940)
            self.assertEqual(saved["result"]["defender"]["defense"]["total"], 53)

    def test_input_file_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "유키 실험.ini"
            original = "[attacker]\ncharacter=유키\n"
            config.write_text(original, encoding="utf-8")
            result = self.invoke("--loadout", config, "--output", config)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(config.read_text(encoding="utf-8"), original)

    def test_invalid_input_does_not_truncate_previous_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "bad.ini"
            result_path = Path(temporary) / "previous.json"
            config.write_text("[attacker]\nweapon=없는 무기\n", encoding="utf-8")
            result_path.write_text("previous", encoding="utf-8")
            result = self.invoke("--loadout", config, "--format", "json", "--output", result_path)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result_path.read_text(encoding="utf-8"), "previous")


if __name__ == "__main__":
    unittest.main()
