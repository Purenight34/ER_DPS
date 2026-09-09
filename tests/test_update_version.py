"""Offline fixtures only: no real API key or network is used."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import requests

import game_data_api as api
import update_version as sync


class ApiTests(unittest.TestCase):
    def setUp(self):
        self.key = patch.object(api, "get_er_api_key", return_value="fixture-key-not-real")
        self.key.start()
        self.addCleanup(self.key.stop)

    def response(self, status=200, data=None):
        response = Mock(status_code=status, headers={})
        response.json.return_value = {"code": 200, "data": data or []}
        return response

    @patch.object(api, "_wait_for_request_slot")
    @patch.object(api.requests, "get")
    def test_auth_has_timeout_and_redirect_is_disabled(self, get, wait):
        get.return_value = self.response(data=[{"code": 11}])
        self.assertEqual(api.get_game_data("Character"), [{"code": 11}])
        kwargs = get.call_args.kwargs
        self.assertEqual(kwargs["timeout"], 15)
        self.assertFalse(kwargs["allow_redirects"])
        self.assertEqual(kwargs["headers"]["x-api-key"], "fixture-key-not-real")

    @patch.object(api, "_wait_for_request_slot")
    @patch.object(api.requests, "get")
    def test_exception_and_response_body_are_not_exposed(self, get, wait):
        secret_marker = "fixture-sensitive-detail"
        cases = [requests.RequestException(secret_marker), self.response(status=403)]
        cases[1].text = secret_marker
        output = io.StringIO()
        for failure in cases:
            get.side_effect = failure if isinstance(failure, Exception) else None
            get.return_value = failure
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                with self.assertRaises(api.EternalReturnApiError) as error:
                    api.get_game_data("Character")
            self.assertNotIn(secret_marker, str(error.exception))
            self.assertTrue(error.exception.__suppress_context__)
        self.assertNotIn(secret_marker, output.getvalue())

    @patch.object(api.time, "sleep")
    @patch.object(api, "_wait_for_request_slot")
    @patch.object(api.requests, "get")
    def test_only_transient_statuses_retry_and_retry_after_is_capped(self, get, wait, sleep):
        for status in (429, 500, 502, 503, 504):
            get.reset_mock()
            response = self.response(status=status)
            response.headers = {"Retry-After": "999999999"}
            get.return_value = response
            with self.assertRaises(api.EternalReturnApiError):
                api.get_game_data("Character")
            self.assertEqual(get.call_count, 4)
        self.assertTrue(all(call.args[0] <= 30 for call in sleep.call_args_list))
        for status in (301, 400, 401, 403, 404, 501):
            get.reset_mock()
            get.return_value = self.response(status=status)
            with self.assertRaises(api.EternalReturnApiError):
                api.get_game_data("Character")
            self.assertEqual(get.call_count, 1)

    def test_request_spacing(self):
        with patch.object(api, "_last_request_started_at", 10.0), \
             patch.object(api.time, "monotonic", side_effect=[10.2, 11.2]), \
             patch.object(api.time, "sleep") as sleep:
            api._wait_for_request_slot()
            self.assertAlmostEqual(sleep.call_args.args[0], 1.0)

    @patch.object(api, "_wait_for_request_slot")
    @patch.object(api.requests, "get")
    def test_public_localization_never_gets_auth(self, get, wait):
        meta = self.response(data={"l10Path": "https://cdn.example.org/Korean.txt"})
        public = Mock(status_code=200, headers={}, content="Character/Name/11┃유키\n".encode())
        get.side_effect = [meta, public]
        self.assertIn("유키", api.get_korean_localization())
        self.assertNotIn("x-api-key", get.call_args.kwargs["headers"])
        self.assertFalse(get.call_args.kwargs["allow_redirects"])

    def test_path_injection_is_rejected(self):
        for table in ("../users", "Character?key=fixture", "Character/other"):
            with self.assertRaises(ValueError):
                api.get_game_data(table)

    @patch.object(api.requests, "get")
    def test_missing_key_never_starts_network(self, get):
        with patch.object(api, "get_er_api_key", side_effect=RuntimeError("missing fixture key")):
            with self.assertRaisesRegex(api.EternalReturnApiError, "unavailable"):
                api.get_game_data("hash")
        get.assert_not_called()

    def test_localization_rejects_nonpublic_or_credential_urls(self):
        for url in ("http://cdn.example.org/file", "https://user:pass@cdn.example.org/file", "https://localhost/file", "https://127.0.0.1/file", "https://[::1]/file", "https://cdn.example.org:8443/file"):
            with self.assertRaises(api.EternalReturnApiError):
                api._validate_public_download_url(url)


class SyncTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.hashes = {name: index for index, name in enumerate(sync.REQUIRED_TABLES, 1)}
        self.tables = {name: [{"code": index}] for index, name in enumerate(sync.REQUIRED_TABLES, 1)}

    def fetch(self, table):
        return self.hashes if table == "hash" else self.tables[table]

    def run_sync(self):
        with patch.object(sync, "get_game_data", side_effect=self.fetch) as get, \
             patch.object(sync, "get_korean_localization", return_value="Character/Name/11┃유키\n") as korean:
            result = sync.sync_version(self.root)
            return result, get.call_args_list, korean.call_count

    def test_initial_snapshot_and_unchanged_cache_need_only_hash(self):
        result, calls, _ = self.run_sync()
        pointer = json.loads((self.root / "current.json").read_text())
        snapshot = self.root / pointer["snapshot"]
        self.assertTrue((snapshot / "manifest.json").is_file())
        self.assertEqual(json.loads((snapshot / "Character.json").read_text()), self.tables["Character"])
        again, calls, localization_calls = self.run_sync()
        self.assertEqual(result["data_version"], again["data_version"])
        self.assertEqual([call.args[0] for call in calls], ["hash"])
        self.assertEqual(localization_calls, 0)

    def test_changed_version_reuses_only_verified_unchanged_tables(self):
        self.run_sync()
        self.hashes["Character"] = 999
        result, calls, localization_calls = self.run_sync()
        self.assertEqual([call.args[0] for call in calls], ["hash", "Character", "hash"])
        self.assertEqual(localization_calls, 1)

    def test_failed_sync_preserves_current_pointer(self):
        self.run_sync()
        before = (self.root / "current.json").read_bytes()
        self.hashes["Character"] = 999
        with patch.object(sync, "get_game_data", side_effect=lambda table: self.hashes if table == "hash" else None):
            with self.assertRaises(sync.SyncError):
                sync.sync_version(self.root)
        self.assertEqual(before, (self.root / "current.json").read_bytes())

    def test_patch_change_during_sync_preserves_current_pointer(self):
        self.run_sync()
        before = (self.root / "current.json").read_bytes()
        first_hashes = {**self.hashes, "Character": 888}
        final_hashes = {**self.hashes, "Character": 999}
        calls = 0

        def changing_fetch(table):
            nonlocal calls
            if table == "hash":
                calls += 1
                return first_hashes if calls == 1 else final_hashes
            return self.tables[table]

        with patch.object(sync, "get_game_data", side_effect=changing_fetch), \
             patch.object(sync, "get_korean_localization", return_value="Character/Name/11┃유키\n"):
            with self.assertRaisesRegex(sync.SyncError, "changed during sync"):
                sync.sync_version(self.root)
        self.assertEqual(before, (self.root / "current.json").read_bytes())

    def test_corrupt_cache_is_not_reused(self):
        self.run_sync()
        pointer = json.loads((self.root / "current.json").read_text())
        (self.root / pointer["snapshot"] / "Character.json").write_text("[]")
        _, calls, _ = self.run_sync()
        self.assertIn("Character", [call.args[0] for call in calls])

    def test_missing_required_table_and_unsafe_hash_key_are_errors(self):
        for hashes in ({"Character": 1}, {**self.hashes, "../outside": 1}):
            with patch.object(sync, "get_game_data", return_value=hashes):
                with self.assertRaises(sync.SyncError):
                    sync.sync_version(self.root)
        self.assertFalse((self.root / "current.json").exists())

    def test_extra_table_must_be_discovered_in_hash(self):
        with patch.object(sync, "get_game_data", side_effect=self.fetch) as get:
            with self.assertRaises(sync.SyncError):
                sync.sync_version(self.root, tables=["NotPublished"])
        self.assertEqual([call.args[0] for call in get.call_args_list], ["hash"])


if __name__ == "__main__":
    unittest.main()
