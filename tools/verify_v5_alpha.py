#!/usr/bin/env python3
"""Verify the Cost Function V5 semantics against the official alpha re-scoring.

Evidence source: docs/official/alpha_test/cade0002_calculation.html
(official "PATH Detail Cost Calculation", Cost Function V5, eval run 2026-07-19).
Analysis: docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md

Checks (all against static numbers in the official page / old report.json):
  1. WL rule  — overlap-midpoint guiding points + corner turn points; the five
     official example-path subtotals must reproduce EXACTLY.
  2. Overflow — single-charge-per-axis-per-traversal demand; the five per-case
     overflow totals and capacity totals must reproduce EXACTLY.
  3. Tail     — piecewise beta_lin*theta*OA + beta_tail*sqrt(R-theta)*OA must
     reproduce the OLD case1 penalty 8,537,517.705283 exactly (the formula is
     shared by the old checker; only WL/demand accounting changed in V5).
  4. Reconstruct per-case V5 ranking cost (page's computed cells are 0.00 by a
     generator bug) and print old-vs-new comparison.

Run from the repo root:  python3 tools/verify_v5_alpha.py     (exit 0 = all pass)
"""
import json
import math
import os
import sys

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                    "docs", "official", "alpha_test", "cade0002")

ALPHA_RANK = {"case0": 0.3, "case1": 0.15, "case2": 0.1, "case3": 0.1, "case4": 0.1}
RUNTIME = {"case0": 0.144, "case1": 0.846, "case2": 3.290, "case3": 4.427, "case4": 5.382}
V5_OVERFLOW = {"case0": 469.23, "case1": 3375.27, "case2": 18647.65,
               "case3": 12764.98, "case4": 19662.17}   # page-displayed (2dp)
V5_CAPACITY = {"case0": 316753.55, "case1": 495478.83, "case2": 2236044.30,
               "case3": 1408446.75, "case4": 3648619.82}
# Official ONE-PATH examples: (PATH declaration, page-displayed subtotal)
EXAMPLES = {
    "case0": ("PATH 500 BLK02 2 CH8 4 CH8 1 CH6 3 CH6 1 CH4 3 CH4 1 BLK05 3", 1175.56850),
    "case1": ("PATH 1 BLK01 1 CH22 3 CH22 1 CH20 3 CH20 1 CH18 3 CH18 4 BLK05 2", 1490.57750),
    "case2": ("PATH 834 BLK01 3 CH7 1 CH7 3 CH10 1 CH10 3 CH13 1 CH13 3 CH16 1 CH16 3 "
              "CH18 1 CH18 3 CH21 1 CH21 3 CH24 1 CH24 3 CH27 1 CH27 3 CH30 1 CH30 3 "
              "CH32 1 CH32 3 CH33 1 CH33 4 BLK04 2", 8606.18400),
    "case3": ("PATH 25 BLK01 3 CH2 1 CH2 3 CH3 1 CH3 3 CH5 1 CH5 3 CH6 1 CH6 3 CH8 1 "
              "CH8 3 CH10 1 CH10 3 CH12 1 CH12 3 CH13 1 CH13 4 BLK04 2", 4225.04700),
    "case4": ("PATH 191 BLK03 2 CH19 4 CH19 1 CH18 3 CH18 1 CH16 3 CH16 1 CH14 3 CH14 1 "
              "CH12 3 CH12 1 CH10 3 CH10 1 CH7 3 CH7 1 CH3 3 CH3 3 BLK19 1", 8085.23100),
}
BETA_LIN, BETA_TAIL, THETA, DELTA = 20.0, 1.5, 0.05, 0.5


class R:
    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h

    def edge(self, e):
        # (axis, fixed coord, span lo, span hi); edges CCW from left: 1=L 2=T 3=R 4=B
        if e == 1: return ('H', self.x, self.y, self.y + self.h)
        if e == 3: return ('H', self.x + self.w, self.y, self.y + self.h)
        if e == 2: return ('V', self.y + self.h, self.x, self.x + self.w)
        if e == 4: return ('V', self.y, self.x, self.x + self.w)
        raise ValueError(e)


def parse_cfg(path):
    blocks, channels, paths, mode, outline = {}, {}, [], None, None
    for line in open(path, encoding="utf-8"):
        t = line.split()
        if not t: continue
        if t[0] == 'OUTLINE': outline = (float(t[1]), float(t[2])); continue
        if t[0] in ('BLOCK', 'CHANNEL'): mode = t[0]; continue
        if t[0] == 'END': mode = None; continue
        if t[0] == 'PATH':
            if len(t) == 1: continue
            paths.append((float(t[1]), [(t[i], int(t[i + 1])) for i in range(2, len(t), 2)]))
            continue
        if mode == 'BLOCK': blocks[t[0]] = R(*map(float, t[1:5]))
        elif mode == 'CHANNEL': channels[t[0]] = R(*map(float, t[1:5]))
    return outline, blocks, channels, paths


def cross_gp(ra, ea, rb, eb):
    """V5 cross guiding point: midpoint of the overlapping border segment
    (fixed coord averaged across the <=0.001 adjacency gap)."""
    axa, ca, lo_a, hi_a = ra.edge(ea)
    _, cb, lo_b, hi_b = rb.edge(eb)
    mid = 0.5 * (max(lo_a, lo_b) + min(hi_a, hi_b))
    fixed = 0.5 * (ca + cb)
    return (fixed, mid) if axa == 'H' else (mid, fixed)


def corner_gp(r, e_in, e_out):
    """V5 internal-turn guiding point: intersection corner of the two edges.
    None when both edges lie on the same axis (straight pass-through)."""
    ax_i, c_i, _, _ = r.edge(e_in)
    ax_o, c_o, _, _ = r.edge(e_out)
    if ax_i == ax_o: return None
    return (c_i, c_o) if ax_i == 'H' else (c_o, c_i)


def path_wl_v5(seq, rects):
    gps = []
    for (na, ea), (nb, eb) in zip(seq, seq[1:]):
        if na == nb:
            g = corner_gp(rects[na], ea, eb)
            if g is not None: gps.append(g)
        else:
            gps.append(cross_gp(rects[na], ea, rects[nb], eb))
    return sum(abs(a[0] - b[0]) + abs(a[1] - b[1]) for a, b in zip(gps, gps[1:]))


def overflow_v5(channels, paths):
    """V5 demand: each traversal charges its nets ONCE per axis touched."""
    demand = {n: {'H': 0.0, 'V': 0.0} for n in channels}
    for nets, seq in paths:
        j = 0
        while j < len(seq):
            name, e = seq[j]
            if name in channels:
                if j + 1 < len(seq) and seq[j + 1][0] == name:
                    e_in, e_out = e, seq[j + 1][1]; j += 2
                else:
                    e_in, e_out = e, e; j += 1
                for ax in {'H' if ee in (1, 3) else 'V' for ee in (e_in, e_out)}:
                    demand[name][ax] += nets
            else:
                j += 1
    ovf = cap = 0.0
    for name, r in channels.items():
        ch, cv = 25.0 * r.h, 25.0 * r.w
        cap += ch + cv
        ovf += max(0.0, demand[name]['H'] - ch) + max(0.0, demand[name]['V'] - cv)
    return ovf, cap


def overflow_penalty_v5(rate, oa):
    if rate <= THETA:
        return BETA_LIN * rate * oa
    return BETA_LIN * THETA * oa + BETA_TAIL * math.sqrt(rate - THETA) * oa


def main():
    fails = 0

    print("=== 1. V5 WL rule vs the five official example subtotals ===")
    for case, (decl, official) in EXAMPLES.items():
        _, blocks, channels, _ = parse_cfg(os.path.join(BASE, case, "output.cfg"))
        t = decl.split()
        seq = [(t[i], int(t[i + 1])) for i in range(2, len(t), 2)]
        mine = path_wl_v5(seq, {**blocks, **channels})
        ok = abs(mine - official) < 5e-6
        fails += not ok
        print(f"  {case}: official={official:12.5f} mine={mine:12.5f} {'EXACT' if ok else 'FAIL'}")

    print("=== 2. V5 overflow demand (single-charge) vs the five case totals ===")
    stats = {}
    for i in range(5):
        case = f"case{i}"
        outline, blocks, channels, paths = parse_cfg(os.path.join(BASE, case, "output.cfg"))
        ovf, cap = overflow_v5(channels, paths)
        # page shows 2dp with round-half-even (case4 truth 19,662.175 prints 19,662.17;
        # capacity truth 3,648,619.8249... prints .82) -> allow half-ulp display slack
        ok = abs(ovf - V5_OVERFLOW[case]) < 0.006 and abs(cap - V5_CAPACITY[case]) < 0.02
        fails += not ok
        print(f"  {case}: ovf={ovf:12,.2f} (page {V5_OVERFLOW[case]:12,.2f})  "
              f"cap={cap:14,.2f} (page {V5_CAPACITY[case]:14,.2f}) {'EXACT' if ok else 'FAIL'}")
        wl = sum(n * path_wl_v5(s, {**blocks, **channels}) for n, s in paths)
        stats[case] = (outline[0] * outline[1], wl, ovf, cap)

    print("=== 3. Piecewise sqrt tail reproduces the OLD case1 penalty ===")
    rep1 = json.load(open(os.path.join(BASE, "case1", "report.json"), encoding="utf-8"))
    cb = rep1["cost_breakdown"]
    got = overflow_penalty_v5(cb["channel_overflow_rate"], cb["outline_area"])
    ok = abs(got - cb["overflow_penalty"]) < 1e-4
    fails += not ok
    print(f"  case1(old R={cb['channel_overflow_rate']:.6f}): formula={got:,.6f} "
          f"official={cb['overflow_penalty']:,.6f} {'EXACT' if ok else 'FAIL'}")

    print("=== 4. V5 per-case ranking-cost reconstruction (page cells are 0.00-bugged) ===")
    total = 0.0
    for i in range(5):
        case = f"case{i}"
        oa, wl, ovf, cap = stats[case]
        cost = (oa + ALPHA_RANK[case] * wl + overflow_penalty_v5(ovf / cap, oa)
                + DELTA * RUNTIME[case] * math.sqrt(oa))
        total += cost
        print(f"  {case}: OA={oa:14,.0f} WL={wl:16,.2f} R={ovf / cap:.6f} cost={cost:16,.2f}")
    print(f"  TOTAL V5 = {total:,.2f}   (old-rule official ranking total = 228,945,364.39)")

    print("PASS" if fails == 0 else f"FAIL ({fails} checks)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
