"""Offline regression tests for the audit manifest guardrails, not API tests."""

import copy
import json
import unittest
from collections import Counter

from tools.validate_api_inventory import ROOT, safe_path, validate_data


class InventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = ROOT / "docs/api"
        cls.original_lock = json.loads((folder / "upstream-lock.json").read_text(encoding="utf-8"))
        cls.original_inventory = json.loads((folder / "inventory.json").read_text(encoding="utf-8"))

    def setUp(self):
        self.lock = copy.deepcopy(self.original_lock)
        self.inventory = copy.deepcopy(self.original_inventory)

    def row(self, name):
        return next(row for row in self.inventory["modules"] if row["id"] == name)

    def errors(self):
        return "\n".join(validate_data(self.lock, self.inventory))

    def recount(self):
        self.inventory["counts"] = dict(Counter(row["decision"] for row in self.inventory["modules"]))

    def test_current_snapshot(self):
        self.assertEqual(validate_data(self.lock, self.inventory), [])

    def test_duplicate_module(self):
        self.inventory["modules"].append(copy.deepcopy(self.inventory["modules"][0]))
        self.assertIn("duplicate module ID", self.errors())

    def test_commit_mismatch(self):
        self.inventory["upstreamCommit"] = "0" * 40
        self.assertIn("commit mismatch", self.errors())

    def test_missing_daily_recommendations(self):
        self.row("recommend_songs")["decision"] = "exclude"
        self.assertIn("required adoption missing: recommend_songs", self.errors())

    def test_private_fm_is_not_excluded_radio(self):
        for name in ("personal_fm", "personal_fm_mode", "fm_trash"):
            self.assertEqual(self.row(name)["decision"], "adopt")
        self.assertEqual(self.errors(), "")

    def test_video_cannot_be_adopted(self):
        row = self.row("comment_video")
        row.update(decision="adopt", category="comment", phase="M2")
        self.recount()
        self.assertIn("excluded business adopted: comment_video", self.errors())

    def test_v2_sdk_cannot_be_silently_adopted(self):
        self.row("register_checktoken_v2").update(decision="adopt", phase="M1")
        self.recount()
        self.assertIn("v2 browser SDK requires an explicit decision", self.errors())

    def test_together_gate_required(self):
        self.row("listentogether_heatbeat")["blockedBy"] = []
        self.assertIn("missing together gate", self.errors())

    def test_source_must_be_pinned(self):
        row = self.row("recommend_songs")
        row["source"]["url"] = row["source"]["url"].replace(self.lock["commit"], "master")
        self.assertIn("unpinned source URL", self.errors())

    def test_source_path_cannot_escape(self):
        self.row("recommend_songs")["source"]["path"] = "../../outside.js"
        self.assertIn("unsafe source path", self.errors())

    def test_observations_cannot_claim_live_verification(self):
        self.row("song_url_v1")["verification"] = "live-verified"
        self.assertIn("unsupported verification claim", self.errors())

    def test_official_source_constraint_required(self):
        self.row("song_url_v1")["constraints"] = []
        self.assertIn("official playback boundary missing", self.errors())

    def test_wrapper_route_exceptions(self):
        self.row("personal_fm")["wrapperRoute"] = "/personal/fm"
        self.assertIn("wrapper route exception lost", self.errors())

    def test_count_mismatch(self):
        self.inventory["counts"]["adopt"] += 1
        self.assertIn("decision counts mismatch", self.errors())

    def test_portable_paths(self):
        for path in ("../escape", "/absolute", "module/../escape", "module\\file.js", "./a", ""):
            with self.subTest(path=path):
                self.assertFalse(safe_path(path))
        self.assertTrue(safe_path("module/song_url_v1.js"))


if __name__ == "__main__":
    unittest.main()
