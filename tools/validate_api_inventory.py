#!/usr/bin/env python3
"""Validate the static API audit; never execute upstream JavaScript or call APIs."""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced"
REQUIRED = {
    "recommend_songs", "personal_fm", "personal_fm_mode", "fm_trash",
    "song_url_v1", "login_qr_key", "login_qr_check", "playlist_create",
    "listentogether_heatbeat", "listentogether_play_command",
}
FORBIDDEN = {
    "api", "eapi_decrypt", "song_url_match", "song_url_ncmget",
    "personalized_mv", "comment_mv", "comment_video", "comment_dj",
    "user_dj", "user_audio", "program_recommend", "personalized_djprogram",
    "record_recent_dj", "record_recent_voice", "record_recent_video",
    "artist_mv", "artist_video", "artist_new_mv", "simi_mv", "top_mv",
    "personalized_privatecontent", "personalized_privatecontent_list",
    "comment_event", "user_event", "user_event_all", "share_resource",
}
FORBIDDEN_PREFIXES = (
    "mv_", "video_", "mlog_", "dj_", "djradio_", "voice_", "voicelist_",
    "broadcast_", "radio_", "yunbei", "event", "topic_", "rep_ugc_",
    "middle_play_", "thinktank_", "ugc_",
)


def safe_path(value):
    """Only portable, relative POSIX file paths are accepted in lock data."""
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and ".." not in path.parts and str(path) == value


def validate_data(lock, inventory):
    errors = []

    def check(condition, message):
        if not condition:
            errors.append(message)

    commit = lock.get("commit", "")
    check(bool(re.fullmatch(r"[0-9a-f]{40}", commit)), "invalid pinned commit")
    check(lock.get("repository") == REPOSITORY, "unexpected reference repository")
    check(lock.get("schemaVersion") == inventory.get("schemaVersion") == 1,
          "unsupported schema version")
    check(inventory.get("upstreamCommit") == commit, "commit mismatch")
    check(lock.get("license") == "MIT", "license requires re-review")
    modules = inventory.get("modules", [])
    ids = [row.get("id", "") for row in modules]
    check(len(ids) == len(set(ids)), "duplicate module ID")
    check(len(ids) == lock.get("moduleCount"), "module count mismatch")
    check(dict(Counter(row.get("decision") for row in modules)) == inventory.get("counts"),
          "decision counts mismatch")
    by_id = {row.get("id"): row for row in modules}
    for name in REQUIRED:
        check(by_id.get(name, {}).get("decision") == "adopt", f"required adoption missing: {name}")
    together = [row for row in modules if row.get("id", "").startswith("listentogether_")]
    check(len(together) == 9 and all(row.get("decision") == "adopt" for row in together),
          "together module coverage changed")
    check(by_id.get("register_checktoken_v2", {}).get("decision") == "defer",
          "v2 browser SDK requires an explicit decision")
    categories = inventory.get("categories", {})
    prefix = f"{REPOSITORY}/blob/{commit}/"
    for row in modules:
        name = row.get("id", "")
        check(bool(re.fullmatch(r"[A-Za-z0-9_-]+", name)), f"invalid module ID: {name}")
        decision = row.get("decision")
        check(decision in {"adopt", "defer", "exclude"}, f"invalid decision: {name}")
        check(bool(row.get("rationale")), f"missing rationale: {name}")
        source = row.get("source", {})
        path = source.get("path", "")
        check(path == f"module/{name}.js" and safe_path(path), f"unsafe source path: {name}")
        check(source.get("url") == prefix + path, f"unpinned source URL: {name}")
        check(bool(re.fullmatch(r"[0-9a-f]{64}", source.get("sha256", ""))),
              f"invalid source hash: {name}")
        check(row.get("verification") == "static-only", f"unsupported verification claim: {name}")
        check(row.get("implementationStatus") == "not-implemented", f"unsupported implementation claim: {name}")
        check(isinstance(row.get("queryFieldsObserved"), list), f"missing observed fields: {name}")
        for call in row.get("requestCalls", []):
            check(call.get("method") == "POST" and isinstance(call.get("line"), int)
                  and call["line"] > 0, f"invalid request observation: {name}")
        if decision == "adopt":
            check(row.get("category") in categories, f"unknown adopted category: {name}")
            check(row.get("phase") in {"M1", "M2", "M3", "M4"}, f"invalid backend phase: {name}")
            forbidden = name in FORBIDDEN or name.lower().startswith(FORBIDDEN_PREFIXES)
            forbidden = forbidden or "_video_" in name or "_mv_" in name
            check(not forbidden, f"excluded business adopted: {name}")
            if name.startswith("listentogether_"):
                check("together-receive-contract" in row.get("blockedBy", []),
                      f"missing together gate: {name}")
        else:
            check(row.get("phase") is None, f"non-adopted item assigned phase: {name}")
    check("official-source-only" in by_id.get("song_url_v1", {}).get("constraints", []),
          "official playback boundary missing")
    check("native-checktoken-v2" in by_id.get("playlist_subscribe", {}).get("blockedBy", []),
          "playlist subscription v2 gate missing")
    for name, route in {"personal_fm": "/personal_fm", "fm_trash": "/fm_trash",
                        "daily_signin": "/daily_signin"}.items():
        check(by_id.get(name, {}).get("wrapperRoute") == route, f"wrapper route exception lost: {name}")
    reference_paths = []
    for row in lock.get("referenceFiles", []):
        path = row.get("path", "")
        reference_paths.append(path)
        check(safe_path(path), f"unsafe reference path: {path}")
        check(row.get("url") == prefix + path, f"unpinned reference URL: {path}")
        check(bool(re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", ""))),
              f"invalid reference hash: {path}")
    check(len(reference_paths) == len(set(reference_paths)), "duplicate reference file")
    check("LICENSE" in reference_paths and "util/request.js" in reference_paths,
          "missing core reference files")
    return errors


def validate_checkout(upstream, lock, inventory):
    errors = []
    upstream = upstream.resolve()
    commit = subprocess.check_output(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
    if commit != lock["commit"]:
        errors.append("upstream HEAD is not the pinned commit")
    tree = subprocess.check_output(
        ["git", "-C", str(upstream), "rev-parse", "HEAD:module"], text=True).strip()
    if tree != lock["moduleTree"]:
        errors.append("upstream module tree mismatch")
    expected = {row["source"]["path"] for row in inventory["modules"]}
    actual = {p.relative_to(upstream).as_posix() for p in (upstream / "module").glob("*.js")}
    if actual != expected:
        errors.append("upstream module coverage mismatch")
    files = lock["referenceFiles"] + [row["source"] for row in inventory["modules"]]
    for row in files:
        path = upstream / row["path"]
        if not path.resolve().is_relative_to(upstream):
            errors.append(f"reference escapes checkout: {row['path']}")
        elif not path.is_file():
            errors.append(f"reference missing: {row['path']}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != row["sha256"]:
            errors.append(f"reference content changed: {row['path']}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, help="Optional pinned, local reference checkout")
    args = parser.parse_args()
    try:
        folder = ROOT / "docs/api"
        lock = json.loads((folder / "upstream-lock.json").read_text(encoding="utf-8"))
        inventory = json.loads((folder / "inventory.json").read_text(encoding="utf-8"))
        errors = validate_data(lock, inventory)
        license_row = next(row for row in lock["referenceFiles"] if row["path"] == "LICENSE")
        license_path = ROOT / "third_party/api-enhanced/LICENSE"
        if hashlib.sha256(license_path.read_bytes()).hexdigest() != license_row["sha256"]:
            errors.append("retained MIT license differs from upstream")
        if args.upstream and not errors:
            errors.extend(validate_checkout(args.upstream, lock, inventory))
        if errors:
            print("API inventory validation failed:\n- " + "\n- ".join(errors), file=sys.stderr)
            return 1
        print(f"API inventory OK: {len(inventory['modules'])} modules, {inventory['counts']}")
        print("Evidence: static audit only; no live API compatibility claim.")
        return 0
    except (OSError, ValueError, KeyError, TypeError, AttributeError, StopIteration,
            subprocess.CalledProcessError) as error:
        print(f"API inventory validation error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
