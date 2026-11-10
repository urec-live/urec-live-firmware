#!/usr/bin/env python3
"""GetMeFit idle soak test: records the beacon while nothing moves it, and counts what it reports anyway.

Leave the beacon still (on the bench, or on a machine nobody uses) and run:

    python tools/idle_soak.py                    # 60 min, beacon found by USB id
    python tools/idle_soak.py --minutes 30 --port COM5
    python tools/idle_soak.py --analyze sessions/2026-11-04_1500_soak.log   # recount any session log

Gives a false-count baseline, so false counts at the gym can be blamed on the machine, not the sensor.
Counted: phantom reps ([rep]), phantom sets ([set] done), drift recalibrations ([cal] resting
reading moved), failed calibrations, [err] lines, magnetometer drop-outs, ignored count events
([int]) and port drops.

The log is the same format as gym_capture.py's (GetMeFit/sessions/<date>_<time>_soak.log), with the summary
appended as SYS lines. Only the read-only commands `s` and `mag` are sent, at the start and at
the end, so the log holds the beacon's state; no setting is changed. The clock starts when the
beacon's port first opens. The firmware has no battery readout, so battery drain is not measured.
"""
from __future__ import annotations

import argparse
import os
import queue
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gym_capture as gc  # noqa: E402  (same folder; shares the port reader and the log format)

RECAL_RE = re.compile(r"^\[cal\] resting reading moved (\d+) mg")
COUNTED = (  # key, label, beacon line
    ("rep", "phantom reps ([rep])", re.compile(r"^\[rep\] ")),
    ("set", "phantom sets ([set] done)", re.compile(r"^\[set\] done")),
    ("recal", "drift recalibrations", RECAL_RE),
    ("calfail", "failed calibrations", re.compile(r"^\[cal\] failed")),
    ("err", "[err] lines", re.compile(r"^\[err\]")),
    ("mag", "magnetometer drop-outs", re.compile(r"^\[weight\] .*(?:stopped answering|does not answer|dropped out)")),
    ("int", "ignored count events ([int])", re.compile(r"^\[int\] ")),
)
PROBE = ("s", "mag")
FINAL_WAIT_S = 2.0


class SoakCount:
    """Counts the soak's events; fed live or from a session log."""

    def __init__(self):
        self.hits = {key: [] for key, _, _ in COUNTED}
        self.first = None
        self.last = None
        self.drops = 0
        self.gap_s = 0.0
        self.down_since = None
        self.gap = gc.GapWatch()
        self.unseen = []  # (when, sets, text) for sets that started while the port was down

    def add(self, ev: gc.Event):
        """Returns the counter key if this line counts."""
        self.first = self.first or ev.ts
        self.last = ev.ts
        if ev.src == "SYS":
            m = gc.LOST_RE.match(ev.text)
            if m and m.group(1) == "beacon":
                self.drops += 1
                self.down_since = ev.ts
                self.gap.lost()
            m = gc.BACK_RE.match(ev.text)
            if m and m.group(1) == "beacon":
                self.gap_s += float(m.group(2))
                self.down_since = None
                self.gap.back(ev.ts)
            return None
        if ev.src != "BCN":
            return None
        m = gc.REP_RE.match(ev.text)
        if m:
            self.gap.rep(int(m.group(2)))
        m = gc.STATUS_RE.match(ev.text)
        if m:
            before = self.gap.before
            n = self.gap.status(int(m.group(3)))
            if n:
                self.unseen.append((ev.ts, n, f"{n} set(s) started while the port was down (set seq {before} -> "
                                           f"{m.group(3)}), or the beacon rebooted"))
                return "unseen"
        for key, _, rx in COUNTED:
            if rx.match(ev.text):
                self.hits[key].append(ev)
                return key
        return None

    def unseen_sets(self) -> int:
        return sum(n for _, n, _ in self.unseen)

    def short(self) -> str:
        h = self.hits
        return (f"{len(h['rep'])} reps, {len(h['set']) + self.unseen_sets()} sets, {len(h['recal'])} recal, "
                f"{len(h['err'])} err, {self.drops} drops")

    def summary(self, start, end) -> list:
        minutes = max((end - start).total_seconds(), 0.0) / 60
        gap = self.gap_s + ((end - self.down_since).total_seconds() if self.down_since else 0.0)
        out = [f"idle soak {start:%Y-%m-%d %H:%M:%S} - {end:%H:%M:%S}: {minutes:.1f} min, "
               f"{self.drops} port drops ({gap:.0f} s not connected)"]
        for key, label, _ in COUNTED:
            n = len(self.hits[key])
            rate = f"  ({n / (minutes / 60):.1f}/h)" if n and minutes > 0 else ""
            extra = ""
            if key == "recal" and n:
                moved = [RECAL_RE.match(e.text).group(1) for e in self.hits[key]]
                extra = f"  moved {', '.join(moved)} mg"
            out.append(f"  {label:<30}{n:>5}{rate}{extra}")
            if key == "set" and self.drops:
                out.append(f"  {'  + sets during port drops':<30}{self.unseen_sets():>5}  (from the set seq)")
        sets = len(self.hits["set"]) + self.unseen_sets()
        out.append("  verdict: " + ("no phantom reps or sets" if len(self.hits["rep"]) + sets == 0 else
                                    f"{len(self.hits['rep'])} phantom reps and {sets} phantom sets "
                                    "with nothing moving the beacon"))
        out.append("  battery: not measured (the firmware has no battery readout)")
        listed = [(e.ts, e.text) for evs in self.hits.values() for e in evs]
        listed += [(ts, text) for ts, _, text in self.unseen]
        if listed:
            out.append("events:")
            out.extend(f"  {gc.clock(ts)} {text}" for ts, text in sorted(listed))
        return out


def show(ev: gc.Event, key) -> None:
    t = ev.text
    if ev.src == "SYS":
        mark = "!!  " if ev.kind.endswith((":lost", ":cut")) else "**  "
    elif ev.src == "BCN":
        if t.startswith(">") or gc.DUMP_ROW_RE.match(t) or t == gc.DUMP_HEADER:
            return  # stream or CSV dump rows: logged, not shown
        mark = "!!  " if key else ""
    else:
        mark = ""
    print(f"{gc.clock(ev.ts)} {mark}{t}", flush=True)


def send_probe(beacon: gc.PortReader, log: gc.SessionLog, cmds=PROBE) -> None:
    for cmd in cmds:
        why = beacon.write_line(cmd)
        ev = gc.Event(gc.now(), "CMD", cmd) if why is None else gc.Event(gc.now(), "SYS", f"NOT sent ({why}): {cmd}")
        log.write(ev)


def run_live(args) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    events: "queue.Queue[gc.Event]" = queue.Queue()
    log = gc.SessionLog(gc.session_path(args.out_dir, args.label))
    beacon = gc.PortReader("beacon", "BCN", args.port, gc.BEACON_VIDS, events)
    count = SoakCount()
    log.write(gc.Event(gc.now(), "SYS", f"idle_soak started: {args.minutes:g} min, beacon {args.port or 'auto'}"))
    print(f"Recording to {log.path}")
    print(f"Leave the beacon still for {args.minutes:g} min (the clock starts when its port opens). "
          "Ctrl+C stops early.\n", flush=True)

    def take(ev: gc.Event) -> None:
        log.write(ev)
        show(ev, count.add(ev))

    beacon.start()
    start = t0 = probe_at = status_at = end_at = None
    early = False
    try:
        while end_at is None or time.monotonic() < end_at:
            try:
                ev = events.get(timeout=0.2)
            except queue.Empty:
                ev = None
            mono = time.monotonic()
            if ev is not None:
                take(ev)
                if ev.kind == "beacon:open" and start is None:
                    start, t0 = ev.ts, mono
                    end_at = t0 + args.minutes * 60
                    probe_at = t0 + 1.5  # after the banner the beacon prints on connect
                    status_at = t0 + args.every * 60
            if count.gap.probe_due(gc.now()):
                log.write(gc.Event(gc.now(), "SYS", "no status line since the reconnect - sending `s`"))
                send_probe(beacon, log, ("s",))
            if probe_at is not None and mono >= probe_at:
                send_probe(beacon, log)
                probe_at = None
            if status_at is not None and mono >= status_at:
                print(f"{gc.clock(gc.now())} **  {(mono - t0) / 60:.1f}/{args.minutes:g} min: {count.short()}",
                      flush=True)
                status_at += args.every * 60
    except KeyboardInterrupt:
        early = True

    end = gc.now()
    try:
        if start is not None:
            send_probe(beacon, log)
            stop_at = time.monotonic() + FINAL_WAIT_S
            while time.monotonic() < stop_at:
                try:
                    take(events.get(timeout=0.2))
                except queue.Empty:
                    pass
    except KeyboardInterrupt:
        pass
    beacon.stop()
    beacon.join(timeout=1.0)
    while not events.empty():
        take(events.get_nowait())

    if start is None:
        lines = ["idle soak: the beacon never connected - nothing measured"]
    else:
        lines = count.summary(start, end)
        if early:
            lines[0] += " (stopped early)"
    print()
    for line in lines:
        print(line)
        log.write(gc.Event(gc.now(), "SYS", "soak: " + line))
    log.close()
    print(f"\nSaved {log.lines} lines to {log.path}", flush=True)
    return 0 if start is not None else 1


def run_analyze(path: str) -> int:
    count = SoakCount()
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            ev = gc.parse_log_line(line)
            if ev is not None:
                count.add(ev)
    if count.first is None:
        print(f"{path}: no session-log lines")
        return 1
    for line in count.summary(count.first, count.last):
        print(line)
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Idle soak test: count what the beacon reports while nothing moves it.")
    p.add_argument("--port", help="beacon port, e.g. COM5 (default: the Feather, found by USB id)")
    p.add_argument("--minutes", type=float, default=60.0, help="how long to record (default: 60)")
    p.add_argument("--every", type=float, default=5.0, help="minutes between progress lines (default: 5)")
    p.add_argument("--label", default="soak", help="end of the log's file name (default: soak)")
    p.add_argument("--out-dir", default=gc.DEFAULT_OUT_DIR, help="folder for the log (default: GetMeFit/sessions)")
    p.add_argument("--analyze", metavar="LOG", help="count the events in an existing session log instead")
    args = p.parse_args(argv)
    if args.analyze:
        return run_analyze(args.analyze)
    if args.minutes <= 0 or args.every <= 0:
        p.error("--minutes and --every must be positive")
    return run_live(args)


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    os._exit(code)
