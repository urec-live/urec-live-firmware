"""Python prototype of gym::RepValidator (lib/gym_core/src/rep_validator.cpp), as tuned on the
2026-11-10 gym sets. The C++ has since gained the stop rule for holds, the tilt-first order and
the tilt watch while a move is undecided: it is the reference now (run_replay.cmd replays with it).
This file is kept for its plots and quick parameter scans.

  python rep_validator_proto.py <csv files> [--plot=<dir>] [name=value ...]  (needs numpy, matplotlib)


API: begin(t, rest_mg, anchored) / add_sample(t, acc_mg) / on_candidate(t) / end_session(t).

Signal: a = acc . u - g0, u = unit(LP 2 Hz of acc): the stack's acceleration along gravity, with no
rectification of side-to-side rattle; low-passed at 4 Hz. Strokes are split where a leaky velocity
(direction only) changes sign; each stroke is integrated from v = 0 and de-drifted so v = 0 at its end.
Turning points (peaks, valleys) of the stack position with hysteresis. Each dip candidate is judged
by its rise (valley -> peak) and its return (peak -> next valley).
"""
import math
import os
import sys

import numpy as np

G = 9.80665
P = dict(
    dir_hz=2.0,        # low-pass for the gravity direction used by the projection
    lp_hz=4.0,         # low-pass on a
    tau=1.0,           # leaky velocity (direction only), s
    tau_bias=6.0,      # slow bias tracker on a, s (mean a over whole cycles is 0)
    hv=0.06,           # m/s the leaky velocity must pass to switch direction
    hyst_mm=40,        # turning point hysteresis
    min_mm=80,         # smallest rep travel (absolute floor)
    match=0.5,         # a rep must reach this fraction of the median travel of the counted reps
    start_match=0.6,   # the first two cycles' returns must agree this well
    late_ms=500,       # a dip this soon after its up-stroke ended still belongs to it
    tilt_deg=35,       # tilt from the reference direction that marks handling
    tau_ref=4.0,       # the tilt reference follows the gravity direction this slowly, s
    clip_mg=1990,      # a sample at or beyond this on any axis is clipped
    clip_unchecked=0,  # 1: a size verdict that involves a clipped stroke is not trusted (counted as unchecked)
)

COUNTED = ("count", "fast", "unchecked")
REJECTED = ("norise", "noreturn", "small", "superseded", "dropped", "tilted")


class Cand:
    def __init__(self, t):
        self.t = t
        self.verdict = "pending"
        self.stage = "rise"     # start -> (rise -> ret) -> done
        self.stroke = -1        # index of the owning up-stroke in strokes (len(strokes) while it runs)
        self.rise = float("nan")
        self.ret = float("nan")
        self.peak = float("nan")
        self.t_peak = None
        self.A = float("nan")
        self.start = False
        self.clipped = False
        self.tilt = 0.0


class Validator:
    def __init__(self, p=None):
        self.p = dict(P) if p is None else p

    # ------------------------------------------------------------------ session
    def begin(self, t, rest_mg, anchored):
        g = np.asarray(rest_mg, float)
        self.g0 = float(np.linalg.norm(g))
        self.gref = g / self.g0
        self.anchored = anchored
        self.t0 = t
        self.tprev = None
        self.dlp = g.copy()
        self.alp = 0.0
        self.bias = 0.0
        self.vhp = 0.0
        self.dir = 0
        self.vr = self.xr = self.T = 0.0   # current stroke, from its start
        self.zc = [0.0, 0.0, 0.0]          # since the leaky velocity's last zero crossing
        self.snap = None                   # current stroke at that crossing: (t, vr, xr, T)
        self.t_start = t
        self.X = 0.0                       # position at the end of the last stroke, mm
        self.strokes = []                  # dicts: t0, t1, D, dir, tilt, clip, anchored
        self.s_tilt = 0.0
        self.s_clip = False
        self.tilt = 0.0
        # turning points
        self.zmode = 0
        self.zhi = (t, 0.0)
        self.zlo = (t, 0.0)
        self.zhi_prov = self.zlo_prov = False
        self.tps = [(t, 0.0, "start")]
        # candidates and counts
        self.cands = []
        self.amps = []
        self.counted = 0     # judged and counted
        self.unchecked = 0   # counted without a check
        self.prov = None
        self.trace = []

    @property
    def published(self):
        return self.counted + self.unchecked

    def live_x(self):
        return self.X + self.xr * 1000.0

    # ------------------------------------------------------------------ samples
    def add_sample(self, t, acc):
        p = self.p
        dt = 0.02 if self.tprev is None else min(0.1, max(0.001, t - self.tprev))
        self.tprev = t
        acc = np.asarray(acc, float)
        kd = 1 - math.exp(-2 * math.pi * p["dir_hz"] * dt)
        self.dlp += kd * (acc - self.dlp)
        n = float(np.linalg.norm(self.dlp))
        u = self.dlp / n if n > 1.0 else self.gref
        a = (float(acc @ u) - self.g0) * G / 1000.0
        self.alp += (1 - math.exp(-2 * math.pi * p["lp_hz"] * dt)) * (a - self.alp)
        a = self.alp
        # tilt against a slowly following reference direction
        self.gref = self.gref + (u - self.gref) * (dt / p["tau_ref"])
        self.gref /= np.linalg.norm(self.gref)
        self.tilt = math.degrees(math.acos(max(-1.0, min(1.0, float(u @ self.gref)))))
        self.s_tilt = max(self.s_tilt, self.tilt)
        if np.any(np.abs(acc) >= p["clip_mg"]):
            self.s_clip = True
        # direction
        self.bias += (a - self.bias) * dt / p["tau_bias"]
        prev = self.vhp
        self.vhp = self.vhp * math.exp(-dt / p["tau"]) + (a - self.bias) * dt
        # stroke integration
        v0 = self.vr
        self.vr += a * dt
        self.xr += 0.5 * (self.vr + v0) * dt
        self.T += dt
        z0 = self.zc[0]
        self.zc[0] += a * dt
        self.zc[1] += 0.5 * (self.zc[0] + z0) * dt
        self.zc[2] += dt
        if (prev > 0) != (self.vhp > 0):
            self.snap = (t, self.vr, self.xr, self.T)
            self.zc = [0.0, 0.0, 0.0]
        want = 1 if self.vhp > p["hv"] else (-1 if self.vhp < -p["hv"] else self.dir)
        if want != self.dir:
            if self.dir != 0 and self.snap is not None:
                te, v, x, T = self.snap
                self._end_stroke(te, v, x, T)
                self.vr, self.xr, self.T = self.zc
                self.t_start = te
            self.dir = want
            self.snap = None
        self._zigzag(t, self.live_x(), final=False)
        self._resolve(t)
        self.trace.append((t, a, self.vhp, self.live_x(), self.tilt, float(np.linalg.norm(acc))))

    def _end_stroke(self, t_end, vr, xr, T):
        if T <= 0:
            return
        first_unanchored = not self.anchored and not self.strokes
        D = (xr - vr * T / 2.0) * 1000.0
        self.strokes.append(dict(t0=self.t_start, t1=t_end, D=D, dir=self.dir, tilt=self.s_tilt,
                                 clip=self.s_clip, anchored=not first_unanchored))
        self.X += D
        self.s_tilt = self.tilt
        self.s_clip = False
        self.vr = self.xr = self.T = 0.0
        self._zigzag(t_end, self.X, final=True)

    # ------------------------------------------------------------------ turning points
    def _zigzag(self, t, x, final):
        """Extremes are stroke-end positions (final); live positions only confirm them. An extreme set
        from a live position at confirmation is provisional and replaced by the next final value."""
        h = self.p["hyst_mm"]
        if final:
            if self.zmode >= 0 and (self.zhi_prov or x > self.zhi[1]):
                self.zhi, self.zhi_prov = (t, x), False
            if self.zmode <= 0 and (self.zlo_prov or x < self.zlo[1]):
                self.zlo, self.zlo_prov = (t, x), False
        if self.zmode >= 0 and not self.zhi_prov and x < self.zhi[1] - h:
            if self.zmode == 0 and self.zhi[0] == self.tps[0][0]:
                self.tps[0] = (self.zhi[0], self.zhi[1], "peak")      # started at the top
            else:
                self.tps.append((self.zhi[0], self.zhi[1], "peak"))
            self.zmode = -1
            self.zlo, self.zlo_prov = (t, x), not final
        elif self.zmode <= 0 and not self.zlo_prov and x > self.zlo[1] + h:
            if self.zmode == 0 and self.zlo[0] == self.tps[0][0]:
                self.tps[0] = (self.zlo[0], self.zlo[1], "valley")    # started at the bottom
            else:
                self.tps.append((self.zlo[0], self.zlo[1], "valley"))
            self.zmode = 1
            self.zhi, self.zhi_prov = (t, x), not final

    # ------------------------------------------------------------------ candidates
    def on_candidate(self, t):
        p = self.p
        c = Cand(t)
        self.cands.append(c)
        if not self.anchored and t <= self.t0 + 1e-6:
            # the dip that started the session: its stroke is not anchored, so it cannot be judged.
            # It counts unchecked once its peak is known, unless a later dip shares that peak.
            c.start = True
            return
        last_just_ended = bool(self.strokes) and t - self.strokes[-1]["t1"] <= p["late_ms"] / 1000.0
        c.tilt = max(self.strokes[-1]["tilt"], self.s_tilt) if last_just_ended else self.s_tilt
        if c.tilt > p["tilt_deg"]:
            self._reject(c, "tilted")     # picked up, flipped: whatever the stack did
            return
        if self.dir > 0:
            c.stroke = len(self.strokes)
        elif last_just_ended and self.strokes[-1]["dir"] > 0:
            c.stroke = len(self.strokes) - 1
        else:
            self._reject(c, "norise")
            c.rise = 0.0
            return
        # an earlier dip in the same up-stroke loses (setup merging into rep 1's rise)
        for o in self.cands:
            if o is c or o.stage == "done" or o.start:
                continue
            if o.stroke == c.stroke and o.verdict == "pending":
                self._reject(o, "superseded")
        if any(o is not c and o.stroke == c.stroke and o.verdict in COUNTED for o in self.cands):
            self._reject(c, "superseded")
            return
        self._fast(c)

    def _reject(self, c, why):
        if c is self.prov:
            self.prov = None
        c.verdict = why
        c.stage = "done"

    def _need(self):
        need = self.p["min_mm"]
        if self.counted >= 2 and self.amps:
            need = max(need, self.p["match"] * float(np.median(self.amps[-5:])))
        return need

    def _swing_base(self):
        """Lowest point of the rise in progress: the last valley, or the running low while still falling."""
        if self.zmode == 1:
            for tt, x, k in reversed(self.tps):
                if k in ("valley", "start"):
                    return x
        return self.zlo[1]

    def _fast(self, c):
        if self.counted < 2:
            return
        if self.live_x() - self._swing_base() >= self._need():
            c.verdict = "fast"
            self.counted += 1

    def _resolve(self, t, final=False):
        # the unanchored start dip last, so a later dip that shares its peak is seen first
        for c in sorted(self.cands, key=lambda c: c.start):
            if c.stage == "rise":
                self._find_peak(c, final)
            if c.stage == "ret":
                self._find_return(c, t, final)

    def _find_peak(self, c, final):
        if not c.start and c.stroke >= len(self.strokes) and not final:
            return
        own = self.strokes[c.stroke] if (not c.start and c.stroke < len(self.strokes)) else None
        t_end = own["t1"] if own else c.t
        pk = None
        for i, (tt, x, k) in enumerate(self.tps):
            if k == "peak" and tt >= t_end - 1e-6:
                pk = (i, tt, x)
                break
        if pk is None:
            if not final:
                return
            if c.start:
                c.verdict, c.stage = "unchecked", "done"
                self.unchecked += 1
                return
            if self.zmode >= 0:
                pk = (len(self.tps), self.zhi[0], self.zhi[1])   # ended while rising: tentative peak
            else:
                self._reject(c, "norise")
                return
        i, tt, x = pk
        vprev = None
        for j in range(min(i, len(self.tps)) - 1, -1, -1):
            if self.tps[j][2] in ("valley", "start"):
                vprev = self.tps[j]
                break
        if vprev is not None and own is not None and vprev[0] > own["t0"] + 0.05:
            self._reject(c, "norise")    # the stroke was a blip inside a fall
            c.rise = 0.0
            return
        c.peak, c.t_peak = x, tt
        # one count per rise: a later dip with the same peak supersedes an earlier undecided one
        for o in self.cands:
            if o is c or o.t_peak != tt:
                continue
            if c.start and o.verdict not in REJECTED:
                self._reject(c, "superseded")
                return
            if o.verdict in ("pending", "held"):
                self._reject(o, "superseded")
            elif o.verdict in COUNTED and c.verdict == "pending":
                self._reject(c, "superseded")
                return
        if c.start:
            c.verdict, c.stage = "unchecked", "done"
            self.unchecked += 1
            return
        base_strokes = [s for s in self.strokes if vprev is not None and s["t0"] >= vprev[0] - 1e-6 and s["t1"] <= tt + 1e-6]
        unanch = (vprev is None) or (not self.anchored and vprev[0] <= self.t0 + 1e-6)
        c.rise = float("nan") if unanch else x - vprev[1]
        c.clipped = any(s["clip"] for s in base_strokes)
        c.stage = "ret"

    def _find_return(self, c, t, final):
        nv = None
        for tt, x, k in self.tps:
            if k == "valley" and tt > c.t_peak:
                nv = (tt, x)
                break
        if nv is None:
            if not final:
                return
            nv = (t, min(self.zlo[1] if self.zmode <= 0 else c.peak, self.X))
        c.ret = c.peak - nv[1]
        c.clipped = c.clipped or any(s["clip"] for s in self.strokes if s["t0"] >= c.t_peak - 1e-6 and s["t1"] <= nv[0] + 1e-6)
        c.stage = "done"
        self._verdict(c)

    def _verdict(self, c):
        p = self.p
        c.A = c.ret if math.isnan(c.rise) else min(c.rise, c.ret)
        if c.verdict == "fast":
            self.amps.append(c.A)
            return
        if c.verdict != "pending":
            return
        need = self._need()
        bad = None
        if c.ret < need and (math.isnan(c.rise) or c.rise >= need):
            bad = "noreturn"
        elif not math.isnan(c.rise) and c.rise < need:
            bad = "norise" if c.rise < p["hyst_mm"] else "small"
        if bad:
            if p["clip_unchecked"] and c.clipped:
                c.verdict = "unchecked"
                self.unchecked += 1
            else:
                c.verdict = bad
            return
        if self.counted == 0:
            if self.prov is None:
                self.prov = c
                c.verdict = "held"
                return
            a, b = self.prov.ret, c.ret
            if min(a, b) / max(a, b) >= p["start_match"]:
                self.prov.verdict = "count"
                c.verdict = "count"
                self.counted = 2
                self.amps += [self.prov.A, c.A]
                self.prov = None
            elif b > a:
                self.prov.verdict = "dropped"
                self.prov = c
                c.verdict = "held"
            else:
                c.verdict = "dropped"
            return
        c.verdict = "count"
        self.counted += 1
        self.amps.append(c.A)

    def end_session(self, t):
        if self.dir != 0 and self.T > 0:
            self._end_stroke(t, self.vr, self.xr, self.T)
        self._resolve(t, final=True)
        if self.prov is not None and self.counted == 0:
            self.prov.verdict = "count"
            self.counted = 1
            self.prov = None


# ---------------------------------------------------------------------- CSV replay
def load(path):
    counts, rows = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# counts_ms="):
                counts += [int(v) for v in line.split("=", 1)[1].split(",") if v]
            elif line and line[0].isdigit():
                q = line.split(",")
                rows.append([int(q[0]), int(q[1]), int(q[2]), int(q[3])])
    a = np.array(rows, dtype=float)
    return a[:, 0] / 1000.0, a[:, 1:4], np.array(counts) / 1000.0


def rest_vector(t, acc):
    """Old recordings have no rest before the set: use the rest after it (last 2 s)."""
    return acc[t > t[-1] - 2.0].mean(0)


def run(path, p=None):
    t, acc, counts = load(path)
    v = Validator(p)
    v.begin(t[0], rest_vector(t, acc), anchored=False)
    ci = 0
    for i in range(len(t)):
        v.add_sample(t[i], acc[i])
        while ci < len(counts) and counts[ci] <= t[i] + 1e-6:
            v.on_candidate(counts[ci])
            ci += 1
    while ci < len(counts):
        v.on_candidate(counts[ci])
        ci += 1
    v.end_session(t[-1])
    return t, acc, counts, v


def fmt(c):
    s = f"{c.t:.1f}:{c.verdict}"
    if c.verdict not in ("superseded", "unchecked", "tilted"):
        r = "?" if math.isnan(c.rise) else f"{c.rise:.0f}"
        b = "?" if math.isnan(c.ret) else f"{c.ret:.0f}"
        s += f"({r}/{b})"
    if c.verdict == "tilted":
        s += f"({c.tilt:.0f}deg)"
    return s


def plot(path, t, acc, counts, v, out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    tr = np.array(v.trace)
    fig, ax = plt.subplots(3, 1, figsize=(16, 9), sharex=True)
    for j, col in enumerate("bgr"):
        ax[0].plot(t, acc[:, j], lw=0.5, color=col, alpha=0.7)
    ax[0].plot(tr[:, 0], tr[:, 5], lw=0.6, color="k")
    ax[0].set_ylim(-1200, 2400)
    tw = ax[0].twinx()
    tw.plot(tr[:, 0], tr[:, 4], lw=0.8, color="m")
    tw.set_ylim(0, 180)
    tw.axhline(v.p["tilt_deg"], color="m", ls=":")
    ax[1].plot(tr[:, 0], tr[:, 1], lw=0.7, label="a")
    ax[1].plot(tr[:, 0], tr[:, 2] * 5, lw=0.9, label="vhp x5")
    ax[1].set_ylim(-6, 6)
    ax[1].legend(fontsize=7)
    ax[2].plot(tr[:, 0], tr[:, 3], lw=0.6, color="0.6")
    xs, ts, X = [0.0], [v.t0], 0.0
    for s in v.strokes:
        X += s["D"]
        xs.append(X)
        ts.append(s["t1"])
    ax[2].plot(ts, xs, "-o", ms=3)
    for tt, x, k in v.tps:
        ax[2].annotate(k[0], (tt, x), fontsize=9, color="r", weight="bold")
    col = {"count": "g", "fast": "g", "held": "g", "unchecked": "c", "tilted": "m", "small": "orange",
           "dropped": "orange"}
    lo, hi = ax[2].get_ylim()
    for c in v.cands:
        for a_ in ax:
            a_.axvline(c.t, color=col.get(c.verdict, "r"), lw=1.0, alpha=0.8)
        ax[2].annotate(c.verdict, (c.t, lo + 0.05 * (hi - lo)), fontsize=8, rotation=90, color=col.get(c.verdict, "r"))
    for a_ in ax:
        a_.grid(alpha=0.3)
    ax[0].set_title(f"{os.path.basename(path)}  old={len(counts)} new={v.published}")
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "r_" + os.path.splitext(os.path.basename(path))[0] + ".png"), dpi=70)
    plt.close(fig)


# Eye-labelled truth (2026-11-11 gym sets): real rep tops among the dips after t = 0.
# The dip at t = 0 starts each old recording and cannot be judged (its stroke is not anchored).
TRUTH = {
    "set201": (6, "known: 6 cycles; setup at 0, release 14.2"),
    "set202": (10, "setup 0 and 2.1 (hold 2.2-3.5); 10 reps 4.9-24.3"),
    "set208": (4, "reps 2.9-12.3; release 14.5"),
    "set210": (4, "C1 (5 planned): reps 2.7-13.0, t=0 maybe rep 1"),
    "set211": (5, "X3 slow (8 planned): reps 1.9-11.6"),
    "set212": (7, "reps 1.7-11.7, 18.9, 22.1; 13.6/24.2 small moves; release 25.3"),
    "set213": (5, "reps 3.0-11.1"),
    "set221": (5, "reps 4.4-14.2; 16.1 small move before the release"),
    "set222": (4, "reps 4.0-12.7 (gateway 4)"),
}


def main(argv):
    plot_dir, paths, over = None, [], {}
    for a in argv:
        if a.startswith("--plot="):
            plot_dir = a.split("=", 1)[1]
        elif "=" in a and not os.path.exists(a):
            k, val = a.split("=", 1)
            over[k] = float(val)
        else:
            paths.append(a)
    p = dict(P)
    p.update(over)
    err = 0
    for path in paths:
        t, acc, counts, v = run(path, p)
        if plot_dir:
            plot(path, t, acc, counts, v, plot_dir)
        name = os.path.basename(path)
        key = name.split("_")[1]
        judged = sum(1 for c in v.cands if not c.start and c.verdict in COUNTED + ("held",))
        tr = TRUTH.get(key)
        mark = ""
        if tr:
            mark = f" truth={tr[0]} {'OK' if judged == tr[0] else 'ERR %+d' % (judged - tr[0])}"
            err += abs(judged - tr[0])
        print(f"{name[:19]} old={len(counts):2d} new={v.published:2d} (after t0: {judged:2d}){mark} | "
              + " ".join(fmt(c) for c in v.cands))
    print(f"total |error| on labelled sets: {err}")


if __name__ == "__main__":
    main(sys.argv[1:])
