#!/usr/bin/env python3
"""Download every set the beacon stored in its flash, as one CSV file per set.

    python tools/download_sets.py                  # beacon found by USB id
    python tools/download_sets.py --port COM6
    python tools/download_sets.py --clear          # then erase the store, if every set arrived whole

Plug the beacon into the laptop after a session (it keeps every recorded set in its QSPI flash,
src/beacon/set_store.h). The tool sends `dumpall` and writes
    GetMeFit/sessions/<YYYY-MM-DD_HHMM>_sets/<NNN>_set<seq>_boot<id>.csv
one per set (sessions/ is git-ignored). Each file holds the same lines as a live `dump`, after a
`# stored ...` line with the store index, set seq, boot id and uptime. The set seq is the number the
beacon printed for that set (`[weight] set 114: ...`); the boot id changes at every power-up.

Nothing is erased unless --clear is given, and then only when every set arrived with all its rows
and a matching CRC. Close any other serial monitor first: only one program can hold the port.
Needs pyserial: run it with PlatformIO's Python, ~/.platformio/penv/Scripts/python.exe.
"""
from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gym_capture as gc  # noqa: E402  (same folder: port lookup, dump formats, sessions folder)

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing - run this with PlatformIO's Python: ~/.platformio/penv/Scripts/python.exe")

STORED_RE = re.compile(r"^# stored index=(\d+) boot=([0-9A-F]{4}) set=(\d+) reps=(\d+) samples=(\d+)")
STORED_END_RE = re.compile(r"^# stored end: (\d+) sets")
REFUSED = ("dumpall works on the USB console only", "no set store", "a dump is running", "the set store is busy")
IDLE_TIMEOUT_S = 15.0    # no line at all for this long: the beacon stopped answering
CLEAR_TIMEOUT_S = 60.0   # erasing 2 MB takes ~10 s


class Record:
    def __init__(self, m: re.Match):
        self.index, self.boot, self.seq = int(m.group(1)), m.group(2), int(m.group(3))
        self.samples = int(m.group(5))
        self.lines = [m.string]
        self.rows = 0
        self.crc_ok = None
        self.ended = False

    def ok(self) -> bool:
        return self.ended and self.crc_ok is True and self.rows == self.samples

    def problem(self) -> str:
        if not self.ended:
            return "cut off before '# end'"
        if self.crc_ok is not True:
            return "CRC mismatch: the flash returned different data"
        return f"{self.rows} of {self.samples} rows"

    def file_name(self) -> str:
        return f"{self.index:03d}_set{self.seq}_boot{self.boot}.csv"


def read_lines(ser, timeout_s: float):
    """Yields lines as they arrive; stops after timeout_s without any."""
    buf = b""
    last = time.monotonic()
    while time.monotonic() - last < timeout_s:
        data = ser.read(ser.in_waiting or 1)
        if not data:
            continue
        last = time.monotonic()
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            yield raw.rstrip(b"\r").decode("utf-8", "replace")


def download(ser) -> tuple[list[Record], int | None]:
    records: list[Record] = []
    current = None
    total = None
    ser.reset_input_buffer()
    ser.write(b"dumpall\n")
    for line in read_lines(ser, IDLE_TIMEOUT_S):
        m = STORED_RE.match(line)
        if m:
            current = Record(m)
            records.append(current)
            print(f"  set {current.seq} (#{current.index}, {current.samples} samples) ...", end="", flush=True)
            continue
        m = STORED_END_RE.match(line)
        if m:
            total = int(m.group(1))
            break
        if line.startswith(REFUSED):
            sys.exit(f"the beacon refused: {line}")
        if current is None or current.ended:
            if line:
                print(f"  (beacon: {line})")  # its own log lines, e.g. [battery]
            continue
        if line.startswith("# crc="):
            current.crc_ok = line == "# crc=ok"
            continue  # not part of the dump format itself
        if line == "# end":
            current.lines.append(line)
            current.ended = True
            print(" ok" if current.ok() else f" PROBLEM: {current.problem()}")
            continue
        if gc.DUMP_ROW_RE.match(line):
            current.rows += 1
            current.lines.append(line)
        elif line.startswith("#") or line == gc.DUMP_HEADER:
            current.lines.append(line)
        elif line:
            print(f"\n  (beacon: {line})")
    if current is not None and not current.ended:
        print(f" PROBLEM: {current.problem()}")
    return records, total


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Download the beacon's stored sets as CSV files.")
    p.add_argument("--port", help="beacon port, e.g. COM6 (default: the Feather, found by USB id)")
    p.add_argument("--out-dir", default=gc.DEFAULT_OUT_DIR, help="parent folder (default: GetMeFit/sessions)")
    p.add_argument("--clear", action="store_true", help="erase the store after a complete, verified download")
    args = p.parse_args(argv)

    port = args.port or gc.find_port(gc.BEACON_VIDS)
    if port is None:
        sys.exit("no beacon on USB - plug it in, or pass --port")
    try:
        ser = serial.Serial(port, gc.BAUD, timeout=0.2)  # DTR asserted: the beacon starts printing
    except serial.SerialException as e:
        sys.exit(f"{port}: {e} - close any other serial monitor")
    with ser:
        time.sleep(1.5)  # the beacon prints its banner and status when a host connects
        print(f"Downloading from {port} ...")
        records, total = download(ser)
        if total is None:
            print("the download stopped before '# stored end' - nothing was erased")
            return 1
        if total == 0:
            print("the store is empty")
            return 0

        out_dir = os.path.join(args.out_dir, dt.datetime.now().strftime("%Y-%m-%d_%H%M") + "_sets")
        os.makedirs(out_dir, exist_ok=True)
        for r in records:
            with open(os.path.join(out_dir, r.file_name()), "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(r.lines) + "\n")
        good = [r for r in records if r.ok()]
        print(f"\n{len(records)} sets written to {out_dir}: {len(good)} complete"
              + ("" if len(good) == len(records) else f", {len(records) - len(good)} with problems (kept anyway)"))
        whole = len(good) == len(records) == total
        if not whole and len(records) != total:
            print(f"the beacon reported {total} sets, {len(records)} arrived")

        if not args.clear:
            print("The sets are still on the beacon. To erase them: download_sets.py --clear, or type `clearsets yes`.")
            return 0 if whole else 1
        if not whole:
            print("not erasing: the download was not complete")
            return 1
        ser.write(b"clearsets yes\n")
        for line in read_lines(ser, CLEAR_TIMEOUT_S):
            if line.startswith("[store]"):
                print(f"  {line}")
            if line.startswith("[store] cleared"):
                return 0
            if line.startswith("[store] could not clear"):
                return 1
        print("no answer to clearsets - check with `sets`")
        return 1


if __name__ == "__main__":
    sys.exit(main())
