#!/usr/bin/env python3
"""Reads Cirrus logs and diagnostic packages (docs/tasks/logging-v2.md §7).

  cirrus_log.py summary  <zip|dir|file>...
  cirrus_log.py timeline <zip|dir|file>... [--around mark|HH:MM:SS|ISO time]
                         [--before 120] [--after 30] [--cat cirrus.ui] [--level W]
  cirrus_log.py symbolize <zip|dir|file>... --search <dir with the build's binaries>...

`summary` prints the package's summary.txt and description.txt, then each session: when it
ran, whether it ended, its marks and its most frequent warnings. `timeline` merges every
record of every file in time order, with the flight recorder's debug records (written
later, in log.context blocks) back where they happened; --around keeps the records near
each user.mark or near a time. `symbolize` turns each app.crash's frames (module and
offset, §5.3) into functions and lines with addr2line, from the binaries of the same build
(the app.start record's build_id says which) with their debug symbols.
"""

import argparse
import collections
import datetime
import io
import pathlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

RECORD = re.compile(r"^(\S+) ([DIWCF]) (\S+)\s+(\S+) (\S+)(.*)$")
LEVELS = "DIWCF"


class Record:
    __slots__ = ("time", "level", "thread", "category", "event", "rest", "line", "source", "order")

    def __init__(self, match, line, source, order):
        self.time = datetime.datetime.fromisoformat(match[1])
        self.level, self.thread, self.category, self.event, self.rest = match.group(2, 3, 4, 5, 6)
        self.line, self.source, self.order = line, source, order


def read_inputs(paths):
    """Yields (name, text) for every log file, and (name, text) of package extras."""
    logs, extras = [], {}
    for given in paths:
        path = pathlib.Path(given)
        if path.is_dir():
            for file in sorted(path.glob("cirrus-*.log")):
                logs.append((file.name, file.read_text(encoding="utf-8", errors="replace")))
        elif zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as package:
                for name in package.namelist():
                    text = package.read(name).decode("utf-8", errors="replace")
                    if name.endswith(".log"):
                        logs.append((pathlib.PurePosixPath(name).name, text))
                    else:
                        extras[name] = text
        else:
            logs.append((path.name, path.read_text(encoding="utf-8", errors="replace")))
    return logs, extras


def records(logs):
    out, order = [], 0
    for name, text in logs:
        for line in io.StringIO(text):
            line = line.rstrip("\n")
            match = RECORD.match(line)
            if not match:
                continue
            try:
                out.append(Record(match, line, name, order))
            except ValueError:
                continue
            order += 1
    return out


def summary(args):
    logs, extras = read_inputs(args.inputs)
    for name in ("summary.txt", "description.txt"):
        if name in extras:
            print(f"== {name}\n{extras[name].rstrip()}\n")
    for name, text in logs:
        recs = records([(name, text)])
        if not recs:
            continue
        ended = any(r.event == "app.exit" for r in recs)
        signal = re.search(r"^fatal signal \d+", text, re.M)
        state = "ended" if ended else f"did not end{', ' + signal[0] if signal else ''}"
        print(f"== {name}: {recs[0].time:%Y-%m-%d %H:%M:%S} .. {recs[-1].time:%H:%M:%S} ({state})")
        for r in recs:
            if r.event in ("user.mark", "app.crash", "app.previous_session_unclean"):
                print(f"   {r.time:%H:%M:%S} {r.event}{r.rest}")
        warnings = collections.Counter(
            f"{r.category} {r.event}" for r in recs if r.level in "WCF")
        for key, count in warnings.most_common(args.top):
            print(f"   {count:5} x {key}")
        print()


def parse_time(text, recs):
    try:
        return datetime.datetime.fromisoformat(text)
    except ValueError:
        pass
    clock = datetime.time.fromisoformat(text)
    day = recs[-1].time
    return day.replace(hour=clock.hour, minute=clock.minute, second=clock.second,
                       microsecond=clock.microsecond)


def timeline(args):
    logs, _ = read_inputs(args.inputs)
    recs = [r for r in records(logs) if not r.event.startswith("log.context_")]
    recs.sort(key=lambda r: (r.time, r.order))
    if not recs:
        return
    if args.around:
        if args.around == "mark":
            centres = [r.time for r in recs if r.event == "user.mark"]
        else:
            centres = [parse_time(args.around, recs)]
        before = datetime.timedelta(seconds=args.before)
        after = datetime.timedelta(seconds=args.after)
        recs = [r for r in recs if any(c - before <= r.time <= c + after for c in centres)]
    if args.cat:
        recs = [r for r in recs if r.category.startswith(args.cat) or r.event == "user.mark"]
    if args.level:
        least = LEVELS.index(args.level)
        recs = [r for r in recs if LEVELS.index(r.level) >= least or r.event == "user.mark"]
    last_source = None
    for r in recs:
        if len(logs) > 1 and r.source != last_source:
            print(f"--- {r.source}")
            last_source = r.source
        print(r.line)


FIELD = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)')


def fields(rest):
    return {key: value.strip('"') for key, value in FIELD.findall(rest)}


def find_module(name, dirs, cache):
    if name not in cache:
        cache[name] = None
        for root in dirs:
            for path in pathlib.Path(root).rglob(name):
                if path.is_file():
                    cache[name] = path
                    break
            if cache[name]:
                break
    return cache[name]


def addr2line(path, offsets):
    """{offset: ["function at file:line", ...]} (inlined frames first)."""
    out = subprocess.run(["addr2line", "-a", "-f", "-C", "-i", "-e", str(path), *offsets],
                         capture_output=True, text=True, check=False).stdout.splitlines()
    result, current, i = {}, None, 0
    while i < len(out):
        if out[i].startswith("0x"):
            current = offsets[len(result)] if len(result) < len(offsets) else out[i]
            result[current] = []
            i += 1
            continue
        function = out[i]
        where = out[i + 1] if i + 1 < len(out) else "??"
        result[current].append(f"{function} at {where}")
        i += 2
    return result


def symbolize(args):
    if not shutil.which("addr2line"):
        sys.exit("symbolize needs addr2line (binutils)")
    logs, _ = read_inputs(args.inputs)
    recs = sorted(records(logs), key=lambda r: (r.time, r.order))
    builds = {fields(r.rest).get("build_id") for r in recs if r.event == "app.start"}
    builds.discard(None)
    if builds:
        print(f"build_id in the logs: {', '.join(sorted(builds))}")
    crashes = []
    for r in recs:
        if r.event == "app.crash":
            crashes.append((r, []))
        elif r.event == "app.crash_frame" and crashes:
            crashes[-1][1].append(fields(r.rest))
    if not crashes:
        print("no app.crash in the logs")
        return
    cache = {}
    for crash, frames in crashes:
        print(f"\n{crash.time:%Y-%m-%d %H:%M:%S} app.crash{crash.rest}")
        by_module = collections.defaultdict(list)
        for frame in frames:
            by_module[frame.get("module", "?")].append(frame.get("offset", "0x0"))
        names = {}
        for module, offsets in by_module.items():
            path = find_module(module, args.search, cache)
            if path:
                for offset, lines in addr2line(path, offsets).items():
                    names[(module, offset)] = lines
        for frame in frames:
            module, offset = frame.get("module", "?"), frame.get("offset", "0x0")
            lines = [line for line in names.get((module, offset), []) if not line.startswith("??")]
            what = "; ".join(lines) or frame.get("symbol") or "?"
            print(f"  #{frame.get('i', '?'):>2} {module}+{offset}  {what}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    s = commands.add_parser("summary", help="sessions, marks and warnings")
    s.add_argument("inputs", nargs="+")
    s.add_argument("--top", type=int, default=10, help="warnings listed per session")
    s.set_defaults(run=summary)
    t = commands.add_parser("timeline", help="all records in time order")
    t.add_argument("inputs", nargs="+")
    t.add_argument("--around", help="'mark', HH:MM:SS (the last day in the logs) or an ISO time")
    t.add_argument("--before", type=float, default=120, help="seconds kept before (120)")
    t.add_argument("--after", type=float, default=30, help="seconds kept after (30)")
    t.add_argument("--cat", help="only categories starting with this (cirrus.ui)")
    t.add_argument("--level", choices=list(LEVELS), help="this level and above")
    t.set_defaults(run=timeline)
    y = commands.add_parser("symbolize", help="crash frames to functions and lines")
    y.add_argument("inputs", nargs="+")
    y.add_argument("--search", nargs="+", required=True, metavar="DIR",
                   help="where the build's binaries (with debug symbols) are")
    y.set_defaults(run=symbolize)
    args = parser.parse_args()
    try:
        args.run(args)
    except BrokenPipeError:
        sys.stdout = None


if __name__ == "__main__":
    main()
