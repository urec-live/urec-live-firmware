#!/usr/bin/env python3
"""GetMeFit gym session recorder.

Records the beacon's USB console (and optionally the gateway's) to a session log while sets are
done on the machine, sends typed commands to the beacon, and logs set tags.

    python tools/gym_capture.py                          # beacon found by USB id, label "gym"
    python tools/gym_capture.py --label legcurl --gateway
    python tools/gym_capture.py --port COM5 --gateway COM6
    python tools/gym_capture.py --list                   # show the serial ports

Typed at the console:
    s, mag, teach 50, log ...    sent to the beacon
    !X3                          tag: the next set to end is X3 (any set id in the procedure doc:
                                 C1, T1-T8, X1-X12, BX4 for machine B, R3b for a re-run ...)
    !X3 reps=11 w=50             tag with the true reps / weight, overriding plan.csv
    !note <text>                 free-text note
    !quit                        end the session (Ctrl+C works too)
Tags are logged, never sent. A new set tag replaces one still waiting for its set.

Log: GetMeFit/sessions/<YYYY-MM-DD_HHMM>_<label>.log, one line per event, flushed as it arrives:
    2026-11-04T14:03:12.345 BCN [rep] 3  (set seq 7)
    <host time>             <src> <text exactly as received>
  BCN beacon line, GWY gateway line, CMD typed command sent to the beacon, TAG typed '!' line,
  SYS recorder event: a port opening, dropping or reconnecting (with the length of the gap),
  a command that could not be sent, and the live tag/set pairing notes.
parse_log_line() reads a line back; idle_soak.py and the analyzer reuse this module.

The beacon prints only while a host holds its port (with DTR asserted, pyserial's default), and
prints its banner and status each time one connects. If the cable drops, the recorder keeps
going, reopens the port, logs the gap, and uses the set seq in the status line to tell how many
sets started unseen (it sends `s` itself if no status line follows the reconnect). The gateway
port is opened with DTR/RTS low so opening it does not reset the ESP32.

Needs Python 3.8+ and pyserial: PlatformIO's ~/.platformio/penv/Scripts/python.exe has both.
"""
from __future__ import annotations

import argparse
import datetime as dt
import os
import queue
import re
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import NamedTuple, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing - run this with PlatformIO's Python: ~/.platformio/penv/Scripts/python.exe")

BAUD = 115200
BEACON_VIDS = (0x239A,)                  # Adafruit: Feather nRF52840 Express
GATEWAY_VIDS = (0x303A, 0x10C4, 0x1A86)  # Espressif native USB, CP210x and CH34x bridges
RETRY_S = 1.0
# GetMeFit/sessions, wherever the script is started from (git-ignored)
DEFAULT_OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sessions")

# The procedure doc's sets: machine A, then the machine-B follow-up (B prefix). A tag may add a
# lowercase "b" for a re-run (R3b). Other ids of the same shape are accepted with a warning.
SET_IDS = (
    ["C1"] + [f"T{i}" for i in range(1, 9)] + [f"X{i}" for i in range(1, 13)]
    + ["R1", "R2", "R3", "R5", "R7", "R8", "E2", "E5"]
    + ["BC1", "BK1", "BK2"] + [f"BT{i}" for i in range(1, 9)] + [f"BX{i}" for i in range(1, 13)]
    + ["BR1", "BR3", "BR5", "BR6", "BR7", "BE3", "BE5"]
)
SET_ID_RE = re.compile(r"(B?[CKTXRE]\d{1,2})(B?)")
SET_ID_HELP = "C1, T1-T8, X1-X12, R1-R8, E1-E6, B-prefixed for machine B, + b for a re-run"
OVERRIDE_KEYS = ("reps", "w")

# Beacon lines (src/beacon/main.cpp, weight_sensing.cpp).
REP_RE = re.compile(r"^\[rep\] (\d+)\s+\(set seq (\d+)\)")
SET_DONE_RE = re.compile(r"^\[set\] done: (\d+) reps")
STATUS_RE = re.compile(r"^phase \w+, reps (\d+), set (active|idle) \(seq (\d+)\)")
DUMP_START_RE = re.compile(r"^# w8 set=(\d+) reps=(\d+) samples=(\d+)")
DUMP_ROW_RE = re.compile(r"^-?\d+(?:,-?\d+){10}$")
DUMP_HEADER = "t_ms,ax_mg,ay_mg,az_mg,tx,ty,tz,lx,ly,lz,valid"
# Ends a set without a "[set] done" line (reset, cal, mode/axis change, a failed recalibration).
SET_CUT_PREFIXES = ("set ended", "[weight] set aborted", "[cal] axis", "[cal] failed")

# The recorder's own SYS lines about the ports.
LOST_RE = re.compile(r"^(beacon|gateway) lost ")
BACK_RE = re.compile(r"^(beacon|gateway) reconnected on \S+ after a ([\d.]+) s gap")

_LOG_RE = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}) (BCN|GWY|CMD|TAG|SYS) ?(.*)$")


class Event(NamedTuple):
    ts: dt.datetime
    src: str        # BCN GWY CMD TAG SYS; IN / EOF for console input (never logged as such)
    text: str
    kind: str = ""  # SYS events from a port: "beacon:open", "beacon:lost", "beacon:back" ...


def now() -> dt.datetime:
    return dt.datetime.now()


def stamp(t: dt.datetime) -> str:
    return t.isoformat(timespec="milliseconds")


def clock(t: dt.datetime) -> str:
    return t.strftime("%H:%M:%S")


def parse_log_line(line: str) -> Optional[Event]:
    """Reads one session-log line back into an Event (None for anything else)."""
    m = _LOG_RE.match(line.rstrip("\r\n"))
    if not m:
        return None
    return Event(dt.datetime.fromisoformat(m.group(1)), m.group(2), m.group(3))


# ---------------------------------------------------------------- tags


@dataclass
class Tag:
    kind: str                     # "set", "note", "quit" or "bad"
    set_id: str = ""
    overrides: dict = field(default_factory=dict)
    text: str = ""
    problems: list = field(default_factory=list)

    def describe(self) -> str:
        extra = "".join(f" {k}={v}" for k, v in self.overrides.items())
        return self.set_id + extra


def parse_tag(line: str) -> Tag:
    """Parses a typed '!' line: !X3, !X3 reps=11 w=50, !note <text>, !quit."""
    body = line.strip()[1:].strip()
    words = body.split(None, 1)
    if not words:
        return Tag("bad", problems=["empty tag"])
    head = words[0]
    rest = words[1] if len(words) > 1 else ""
    if head.lower() == "note":
        return Tag("note", text=rest)
    if head.lower() == "quit":
        return Tag("quit")
    m = SET_ID_RE.fullmatch(head.upper())
    if not m:
        return Tag("bad", problems=[f"'{head}' is not a set id ({SET_ID_HELP}) or note/quit"])
    base, rerun = m.group(1), m.group(2).lower()
    tag = Tag("set", set_id=base + rerun)
    if base not in SET_IDS:
        tag.problems.append(f"{base} is not in the procedure's set list")
    for item in rest.split():
        key, sep, value = item.partition("=")
        key = key.lower()
        if not sep or not value:
            tag.problems.append(f"ignored '{item}' (overrides look like reps=11 w=50)")
        elif key not in OVERRIDE_KEYS:
            tag.problems.append(f"ignored '{item}' (known overrides: reps=, w=)")
        elif key == "reps":
            if value.isdigit():
                tag.overrides["reps"] = int(value)
            else:
                tag.problems.append(f"ignored '{item}' (reps must be a whole number)")
        else:
            try:
                ok = float(value) > 0
            except ValueError:
                ok = False
            if ok:
                tag.overrides["w"] = value
            else:
                tag.problems.append(f"ignored '{item}' (w must be a positive number)")
    return tag


# ---------------------------------------------------------------- ports


def find_port(vids) -> Optional[str]:
    for p in sorted(list_ports.comports(), key=lambda p: p.device):
        if p.vid in vids:
            return p.device
    return None


def print_ports() -> None:
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        print("no serial ports")
    for p in ports:
        ids = f"{p.vid:04X}:{p.pid:04X}" if p.vid is not None else "no USB id"
        role = "  <- beacon" if p.vid in BEACON_VIDS else "  <- gateway?" if p.vid in GATEWAY_VIDS else ""
        print(f"{p.device:8} {ids:10} {p.description}{role}")


class PortReader(threading.Thread):
    """Reads one serial port into `events` line by line, and reopens it whenever it drops."""

    def __init__(self, label: str, src: str, port: Optional[str], vids, events: queue.Queue,
                 reset_safe: bool = False):
        super().__init__(name=f"{label}-reader", daemon=True)
        self.label = label            # "beacon" / "gateway", starts its SYS lines
        self.src = src
        self.fixed_port = port        # None: look the board up by USB vendor id on every attempt
        self.vids = vids
        self.events = events
        self.reset_safe = reset_safe  # open with DTR/RTS low (the ESP32 auto-reset circuit)
        self._ser = None
        self._lock = threading.Lock()
        self._halt = threading.Event()

    def _sys(self, text: str, kind: str = "") -> None:
        self.events.put(Event(now(), "SYS", f"{self.label} {text}", f"{self.label}:{kind}" if kind else ""))

    def _open(self):
        port = self.fixed_port or find_port(self.vids)
        if port is None:
            ids = "/".join(f"{v:04X}" for v in self.vids)
            return None, f"no {self.label} on USB (vendor id {ids}) - plug it in, or pass the port"
        try:
            ser = serial.serial_for_url(port, baudrate=BAUD, timeout=0.1, write_timeout=2, do_not_open=True)
            if self.reset_safe:
                ser.dtr = False
                ser.rts = False
            ser.open()
            return ser, None
        except (serial.SerialException, OSError, ValueError) as e:
            hint = " - close any other serial monitor" if "denied" in str(e).lower() else ""
            return None, f"{port}: {e}{hint}"

    def _close(self) -> None:
        with self._lock:
            ser, self._ser = self._ser, None
        if ser is not None:
            try:
                ser.close()
            except (serial.SerialException, OSError):
                pass

    def run(self) -> None:
        buf = b""
        lost_at = None    # (monotonic, wall clock) of the last drop
        said = None       # the last "waiting" reason, so each is logged once, not every second
        while not self._halt.is_set():
            if self._ser is None:
                ser, why = self._open()
                if ser is None:
                    if why != said:
                        self._sys(f"waiting: {why}")
                        said = why
                    self._halt.wait(RETRY_S)
                    continue
                with self._lock:
                    self._ser = ser
                port = ser.port
                said = None
                if lost_at is None:
                    self._sys(f"opened {port}", "open")
                else:
                    gap = time.monotonic() - lost_at[0]
                    self._sys(f"reconnected on {port} after a {gap:.1f} s gap (lost at {clock(lost_at[1])}); "
                              "nothing it printed in the gap was received", "back")
                    lost_at = None
            try:
                ser = self._ser
                data = ser.read(ser.in_waiting or 1)
            except (serial.SerialException, OSError, TypeError, AttributeError) as e:
                # TypeError/AttributeError: pyserial on Windows when the handle vanishes mid-read
                if self._halt.is_set():
                    break
                lost_at = (time.monotonic(), now())
                if buf:
                    self.events.put(Event(now(), self.src, buf.rstrip(b"\r").decode("utf-8", "replace")))
                    self._sys("the line above was cut off by the drop", "cut")
                    buf = b""
                self._sys(f"lost {port}: {e}", "lost")
                self._close()
                continue
            if not data:
                continue
            ts = now()
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self.events.put(Event(ts, self.src, raw.rstrip(b"\r").decode("utf-8", "replace")))
        self._close()

    def write_line(self, text: str) -> Optional[str]:
        """Sends one console line. Returns None when sent, otherwise why it was not."""
        with self._lock:
            ser = self._ser
            if ser is None:
                return "port not connected"
            try:
                ser.write((text + "\n").encode("utf-8"))
            except (serial.SerialException, OSError) as e:
                return str(e)
        return None

    def stop(self) -> None:
        self._halt.set()


# ---------------------------------------------------------------- session log


def session_path(out_dir: str, label: str, when: Optional[dt.datetime] = None) -> str:
    when = when or now()
    safe = re.sub(r"[^A-Za-z0-9_-]+", "-", label).strip("-") or "session"
    base = os.path.join(out_dir, f"{when:%Y-%m-%d_%H%M}_{safe}")
    path, n = base + ".log", 2
    while os.path.exists(path):
        path, n = f"{base}_{n}.log", n + 1
    return path


class SessionLog:
    def __init__(self, path: str):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self.path = path
        self.lines = 0
        self._f = open(path, "x", encoding="utf-8", newline="\n", buffering=1)  # line-buffered

    def write(self, ev: Event) -> None:
        self._f.write(f"{stamp(ev.ts)} {ev.src} {ev.text}\n")
        self.lines += 1

    def close(self) -> None:
        self._f.close()


class GapWatch:
    """Counts the sets that started while the beacon's port was down, from the set seq numbers.

    The beacon prints its status (with the seq) when a host connects, but pyserial's open() flushes
    whatever arrived meanwhile, so that line can be lost: probe_due() says when to ask with `s`."""

    PROBE_AFTER_S = 1.5

    def __init__(self):
        self.seq: Optional[int] = None     # last set seq seen
        self.before: Optional[int] = None  # seq when the port dropped
        self.seen: set = set()             # seqs seen in [rep] lines since the drop
        self.checking = False              # a drop happened and no status line has followed yet
        self.back_at: Optional[dt.datetime] = None
        self.probed = False

    def lost(self) -> None:
        if not self.checking:  # a second drop before the check keeps the first baseline
            self.before, self.seen, self.checking = self.seq, set(), True
        self.back_at = None

    def back(self, ts: dt.datetime) -> None:
        self.back_at, self.probed = ts, False

    def rep(self, seq: int) -> None:
        self.seq = seq
        if self.checking:
            self.seen.add(seq)

    def status(self, seq: int) -> Optional[int]:
        """Feeds a status line's seq; after a drop, returns how many sets started unseen."""
        checking, self.checking, self.back_at = self.checking, False, None
        self.seq = seq
        if not checking or self.before is None:
            return None
        started = (seq - self.before) % 255  # seq runs 1..255
        return max(started - len(self.seen - {self.before}), 0)

    def probe_due(self, ts: dt.datetime) -> bool:
        if self.back_at is None or self.probed or (ts - self.back_at).total_seconds() < self.PROBE_AFTER_S:
            return False
        self.probed = True
        return True


def stdin_reader(events: queue.Queue) -> None:
    """Console input on its own thread (Windows has no select() on stdin)."""
    try:
        for raw in sys.stdin:
            events.put(Event(now(), "IN", raw.rstrip("\r\n")))
    except (OSError, ValueError, UnicodeDecodeError):
        pass
    events.put(Event(now(), "EOF", ""))


# ---------------------------------------------------------------- the recorder


class Capture:
    """Logs every event, keeps the console readable, and follows the sets to pair tags live."""

    def __init__(self, log: SessionLog, beacon: PortReader, echo_all: bool = False):
        self.log = log
        self.beacon = beacon
        self.echo_all = echo_all
        self.done = False
        self.pending: Optional[Tag] = None  # waits for the next "[set] done"
        self.used: dict = {}                # set id -> seq it was paired with
        self.seq: Optional[int] = None      # set seq of the last [rep] or status line
        self.set_open = False               # its [rep] lines were seen, its end was not
        self.gap = GapWatch()
        self.open_at_loss = False           # a set was open when the beacon's port dropped
        self.dump = None                    # [set, samples, rows so far] while a CSV dump streams
        self.stream_hint = False

    # -- output

    def show(self, ts: dt.datetime, text: str, mark: str = "") -> None:
        print(f"{clock(ts)} {mark}{text}", flush=True)

    def note(self, ts: dt.datetime, text: str, warn: bool = False) -> None:
        self.log.write(Event(ts, "SYS", text))
        self.show(ts, text, "!!  " if warn else "**  ")

    # -- events

    def handle(self, ev: Event) -> None:
        if ev.src == "IN":
            self.on_input(ev)
            return
        if ev.src == "EOF":
            self.show(ev.ts, "console input closed - still recording (Ctrl+C stops)", "**  ")
            return
        self.log.write(ev)
        if ev.src == "SYS":
            bad = ev.kind.endswith((":lost", ":cut"))
            self.show(ev.ts, ev.text, "!!  " if bad else "**  ")
            if ev.kind == "beacon:lost":
                if not self.gap.checking:
                    self.open_at_loss = self.set_open
                self.gap.lost()
                if self.dump is not None:
                    self.note(ev.ts, f"CSV dump of set {self.dump[0]} cut off at {self.dump[2]} of {self.dump[1]} rows",
                              warn=True)
                    self.dump = None
            if ev.kind == "beacon:back":
                self.gap.back(ev.ts)
        elif ev.src == "GWY":
            self.show(ev.ts, ev.text, "GW  ")
        else:
            self.on_beacon(ev)

    def on_beacon(self, ev: Event) -> None:
        t = ev.text
        if t.startswith(">"):  # Teleplot stream (`st`), 50 Hz
            if self.echo_all:
                self.show(ev.ts, t)
            elif not self.stream_hint:
                self.stream_hint = True
                self.note(ev.ts, "Teleplot stream lines (>a, >tx ...) are logged but not shown; `st` turns it off")
            return
        if self.dump is not None and self.on_dump_line(ev):
            return
        m = DUMP_START_RE.match(t)
        if m:
            self.dump = [int(m.group(1)), int(m.group(3)), 0]
            self.show(ev.ts, t)
            return
        self.show(ev.ts, t)
        m = REP_RE.match(t)
        if m:
            self.seq, self.set_open = int(m.group(2)), True
            self.gap.rep(self.seq)
            return
        m = SET_DONE_RE.match(t)
        if m:
            self.on_set_done(ev, int(m.group(1)))
            return
        if self.set_open and t.startswith(SET_CUT_PREFIXES):
            self.set_open = False
            waiting = f"; tag {self.pending.set_id} waits for the next set" if self.pending else ""
            self.note(ev.ts, f"set seq {self.seq} was cut short with no [set] done, so no tag pairs with it{waiting}",
                      warn=True)
            return
        m = STATUS_RE.match(t)
        if m:
            self.on_status(ev, m.group(2) == "active", int(m.group(3)))

    def on_dump_line(self, ev: Event) -> bool:
        """Takes a line of the CSV dump in progress. False for any other line: console replies and
        log lines can arrive between dump lines."""
        t = ev.text
        set_no, samples, rows = self.dump
        if DUMP_ROW_RE.match(t):
            self.dump[2] += 1
        elif t == "# end":
            self.dump = None
            ok = rows == samples
            self.note(ev.ts, f"CSV dump of set {set_no}: {rows} rows logged" + ("" if ok else f" of {samples}"),
                      warn=not ok)
            return True
        elif t.startswith("# dump aborted"):
            self.dump = None
            self.note(ev.ts, f"CSV dump of set {set_no} aborted by a new set after {rows} of {samples} rows",
                      warn=True)
            return True
        elif t.startswith("# w8 "):
            self.note(ev.ts, f"CSV dump of set {set_no} never reached '# end' ({rows} of {samples} rows)", warn=True)
            self.dump = None
            return False
        elif not (t == DUMP_HEADER or t.startswith("# ")):
            return False
        if self.echo_all:
            self.show(ev.ts, t)
        return True

    def on_set_done(self, ev: Event, reps: int) -> None:
        self.set_open = False
        seq = "?" if self.seq is None else self.seq
        tag, self.pending = self.pending, None
        if tag is None:
            self.note(ev.ts, f"set seq {seq} ended ({reps} reps) with no tag", warn=True)
            return
        self.used[tag.set_id] = seq
        said = tag.overrides.get("reps")
        diff = f" (tag says {said})" if said is not None and said != reps else ""
        self.note(ev.ts, f"{tag.set_id} = set seq {seq}: {reps} reps counted{diff}")

    def on_status(self, ev: Event, active: bool, seq: int) -> None:
        before = self.gap.before
        unseen = self.gap.status(seq)
        if unseen:
            self.note(ev.ts, f"set seq went {before} -> {seq} while the port was down: {unseen} set(s) "
                             "started unseen, or the beacon rebooted", warn=True)
            if self.pending:
                self.note(ev.ts, f"tag {self.pending.set_id} is still waiting - retype it if its set "
                                 "happened in the gap", warn=True)
        elif unseen == 0 and self.open_at_loss and seq == before and not active:
            self.note(ev.ts, f"set seq {seq} ended while the port was down: its [set] done was lost", warn=True)
        if unseen is not None:
            self.open_at_loss = False
        self.seq = seq
        self.set_open = active

    def tick(self, ts: dt.datetime) -> None:
        """Called on every pass of the main loop."""
        if self.gap.probe_due(ts):
            self.note(ts, "no status line since the reconnect - sending `s` to check for missed sets")
            why = self.beacon.write_line("s")
            self.log.write(Event(ts, "CMD", "s") if why is None else Event(ts, "SYS", f"NOT sent ({why}): s"))

    def on_input(self, ev: Event) -> None:
        line = ev.text.strip()
        if not line:
            return
        if not line.startswith("!"):
            why = self.beacon.write_line(line)
            if why is None:
                self.log.write(Event(ev.ts, "CMD", line))
            else:
                self.note(ev.ts, f"NOT sent to the beacon ({why}): {line}", warn=True)
            return
        self.log.write(Event(ev.ts, "TAG", line))
        tag = parse_tag(line)
        for problem in tag.problems:
            self.show(ev.ts, f"tag: {problem}", "!!  ")
        if tag.kind == "quit":
            self.done = True
        elif tag.kind == "note":
            self.show(ev.ts, "note logged", "**  ")
        elif tag.kind == "set":
            if self.pending is not None:
                self.show(ev.ts, f"replaces tag {self.pending.describe()}, which no set had ended for", "!!  ")
            if tag.set_id in self.used:
                self.show(ev.ts, f"{tag.set_id} was already paired with set seq {self.used[tag.set_id]}", "!!  ")
            self.pending = tag
            self.show(ev.ts, f"next set to end = {tag.describe()}", "**  ")

    def finish(self, ts: dt.datetime) -> None:
        if self.pending is not None:
            self.note(ts, f"tag {self.pending.describe()} never paired: no set ended after it", warn=True)
        if self.dump is not None:
            self.note(ts, f"session ended during the CSV dump of set {self.dump[0]}", warn=True)


# ---------------------------------------------------------------- main


def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Record a GetMeFit gym session: beacon (and gateway) console to a log, with set tags.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="typed lines: beacon commands (s, mag, teach 50, log ...) are sent; "
               "!X3 [reps=11] [w=50] tags the next set to end; !note <text>; !quit")
    p.add_argument("--port", help="beacon port, e.g. COM5 (default: the Feather, found by USB id); "
                                  "any pyserial URL works, e.g. socket://localhost:7777")
    p.add_argument("--gateway", nargs="?", const="auto", metavar="PORT",
                   help="also record the gateway (port optional: found by USB id)")
    p.add_argument("--label", default="gym", help="end of the log's file name (default: gym)")
    p.add_argument("--out-dir", default=DEFAULT_OUT_DIR, help="folder for the logs (default: GetMeFit/sessions)")
    p.add_argument("--echo-all", action="store_true",
                   help="also show CSV dump rows and Teleplot lines on the console (they are always logged)")
    p.add_argument("--list", action="store_true", help="list the serial ports and exit")
    args = p.parse_args(argv)
    if args.gateway and args.port and args.gateway == args.port:
        p.error("--port and --gateway name the same port")
    return args


def main(argv=None) -> int:
    args = parse_args(argv)
    if args.list:
        print_ports()
        return 0
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    events: queue.Queue = queue.Queue()
    log = SessionLog(session_path(args.out_dir, args.label))
    beacon = PortReader("beacon", "BCN", args.port, BEACON_VIDS, events)
    readers = [beacon]
    if args.gateway:
        gw_port = None if args.gateway == "auto" else args.gateway
        readers.append(PortReader("gateway", "GWY", gw_port, GATEWAY_VIDS, events, reset_safe=True))
    cap = Capture(log, beacon, args.echo_all)

    started = now()
    log.write(Event(started, "SYS", f"gym_capture started: label {args.label}, beacon {args.port or 'auto'}, "
                                    f"gateway {args.gateway or 'off'}"))
    print(f"Recording to {log.path}")
    print("Type beacon commands (s, mag, teach 50, log ...), !X3 [reps=11] [w=50] to tag the next set,")
    print("!note <text>, and !quit or Ctrl+C to stop.\n", flush=True)

    for r in readers:
        r.start()
    if sys.stdin is not None:
        threading.Thread(target=stdin_reader, args=(events,), name="stdin", daemon=True).start()

    try:
        while not cap.done:
            try:
                cap.handle(events.get(timeout=0.2))  # short, so Ctrl+C is seen promptly on Windows
            except queue.Empty:
                pass
            cap.tick(now())
    except KeyboardInterrupt:
        pass
    finally:
        for r in readers:
            r.stop()
        for r in readers:
            r.join(timeout=1.0)
        while True:  # whatever the ports delivered before they closed
            try:
                ev = events.get_nowait()
            except queue.Empty:
                break
            if ev.src not in ("IN", "EOF"):
                cap.handle(ev)
        end = now()
        cap.finish(end)
        log.write(Event(end, "SYS", f"session ended after {(end - started).total_seconds() / 60:.1f} min"))
        log.close()
        print(f"\nSaved {log.lines} lines to {log.path}", flush=True)
    return 0


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    # os._exit: the stdin thread may still be blocked in a read, and a normal interpreter
    # shutdown can then abort with "could not acquire lock for <stdin>".
    os._exit(code)
