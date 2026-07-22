#!/usr/bin/env python3
"""Local replica of the official ICCAD 2026 Problem E checker cost.

Milestone C1 acceptance gate: independently recompute the official checker's cost from a solved
``output.cfg`` (OUTLINE / BLOCK / CHANNEL / PATH sections, contest output format -- see
``src/io.cpp``'s ``writeCfg``) plus the matching input ``.csv`` (alpha, block AREA / TYPE / FT
CONVERSION rates -- see ``src/io.cpp``'s ``parseProblem``), and compare against the official
checker's own ``report.json`` output.

Formula (LOCKED by the C1 task brief; the beta_lin*OutlineArea*R term is byte-verified exact against
docs/official/alpha_test/cade0002/case{0..4}/report.json's "cost_breakdown" block on all 5 cases --
see module note 4 below for the one narrow, documented gap in the beta_tail term):

    Cost = OutlineArea + alpha*WL + overflow_penalty + ft_penalty
    overflow_penalty = beta_lin*OutlineArea*R + beta_tail*OutlineArea*max(0, R - theta)
    R = channel_overflow_nets / channel_total_capacity
    channel_overflow_nets = sum over channels of
        max(0, horizontal_usage - 25*h) + max(0, vertical_usage - 25*w)      (nets)
    channel_total_capacity = sum over channels of (25*h + 25*w)              (nets; RAW, unclamped)
    ft_penalty = beta_ft * ft_area_excess
    beta_lin=20.0, beta_tail=1.5, theta=0.05, beta_ft=3.0 (src/model.hpp kBetaLin/kBetaTail/
    kOverflowRateThreshold/kBetaFt -- kept in sync manually; this script has no C++ dependency).

Independently RECOMPUTED by this script, EXACTLY matching report.json (validated to float
precision on all 5 alpha_test cases -- see the pred/report table this script prints):
  - OutlineArea: from the cfg's own OUTLINE line (W*H).
  - channel_overflow_nets / channel_total_capacity: by replaying each PATH's channel touches into
    per-channel usage -- EVERY (object, edge) occurrence in a PATH's step list that names a channel
    contributes `nets` to that channel's horizontal (edge in {1,3}) or vertical (edge in {2,4})
    usage, independently for the entry-hop AND exit-hop of a pass-through visit (see module note 2
    below) -- then applying raw 25*h/25*w capacity from the cfg's own CHANNEL rects.
  - ft_area_excess: by replaying which SOFT blocks appear as PATH intermediates (feedthrough) and
    how many nets cross them, then applying the same ft.hpp formula (ftTierRate/ftExtend/
    ftRequiredArea) against each such block's CSV AREA column (input nominal area) and its cfg
    placed w*h. (Not exercised by the alpha_test set -- always 0 -- but implemented per spec.)

total_wirelength: FALLBACK, read from report.json -- NOT independently recomputed to full fidelity.
See module note 3 below (KNOWN GAP). This script still computes an independent geometric ESTIMATE
(see `guide_point`/`compute_cost`'s `total_wirelength` field) for diagnostic purposes, clearly
labeled "independent estimate, NOT exact" in the printed table -- it is within roughly 1-13% of
report.json across the 5 cases, not the <=0.1% this script otherwise achieves for every other
quantity, so it is NOT used for the pass/fail cost gate. The gate instead plugs report.json's own
total_wirelength into the (independently-verified-correct) Cost formula above, per this task's own
sanctioned fallback clause: "if replaying to full fidelity is too involved to match exactly in one
pass, fall back ... and clearly label which quantities are independently recomputed vs read from
report.json." Everything else in the Cost formula (OutlineArea, overflow_penalty, ft_penalty) is
independently recomputed; only the alpha*WL term's WL input is read from report.json.

Two non-obvious things this replay gets right, discovered by cross-referencing case0's
routing_details/channel.demand_h_by_channel/demand_v_by_channel against a first-draft
implementation that used routing.cpp's own internal conventions verbatim (which turned out NOT to
match the checker's on both counts):

  1. Guide points must be derived from the PATH tokens' explicit edge label, not by re-deriving
     "does rect a touch rect b" via a coordinate-equality epsilon on the cfg's printed (3-decimal)
     coordinates. The checker itself tolerates and FLAGS such gaps as "borderline" (e.g. case0
     path_idx1 step0: BLK02.right()=1827.948 vs CH11.x=1827.947, gap=0.001) -- a tight epsilon
     (routing.cpp's internal kEps=1e-6, which is fine pre-text-round-trip) misclassifies these as
     non-touching and derives the wrong guide point. The edge label was computed by the router
     BEFORE text round-tripping and is authoritative; use it to pick the branch (and, within a
     branch, routing.cpp's guidePoint always takes the .right()/.top() of whichever of {a,b} is the
     left/below rect -- edge3 (a.right()==b.x) => x=a.right(); edge1 (a.x==b.right()) => x=b.right();
     edge2 (a.top()==b.y) => y=a.top(); edge4 (a.y==b.top()) => y=b.top()).
  2. A channel's usage is accrued PER HOP-TOUCH, not per through-traversal direction. A straight
     pass-through channel (entry and exit on the same axis, e.g. left-in/right-out) is charged
     2*nets on that axis -- confirmed exactly against case0: CH6 carries a single 500-net
     straight-through path yet report.json's demand_h_by_channel.CH6 is 1000 (500 for the entry
     touch + 500 for the exit touch); CH8's split entry-vertical/exit-horizontal path similarly
     reproduces demand_h_by_channel.CH8=500 / demand_v_by_channel.CH8=2300 exactly. This differs
     from routing.cpp's OWN internal commitRoutePath, which prices a pass-through channel's usage
     ONCE per direction via the net entry-to-exit guide-point delta (a documented, deliberate
     router-side simplification/proxy -- see routing.cpp's channel-hop comment in findRoutePath) --
     that internal convention is what the router uses to find paths, not what the official checker
     charges, so this script intentionally does NOT mirror it.
  3. KNOWN GAP (why total_wirelength falls back to report.json): the official checker's guiding
     point for a channel hop is NOT the touched boundary's overlap midpoint -- routing.cpp's
     guidePoint() formula (which this task's brief characterized as already-correct "router
     physics"). Ground truth from the checker's own per-path SVG visualizations
     (docs/official/alpha_test/cade0002/case0/viz/path_{0..4}.html), which each embed a dashed
     <polyline> of the actual guide points used AND a title with that exact path's WL (verified: the
     polyline's own Manhattan length, converted from SVG pixel space back to um via the panel's
     rect-vs-cfg-OUTLINE scale factor, reproduces the title's WL to within SVG-text rounding):
       - path_idx0 (case0, BLK02->CH8->BLK03, 900 nets, a plain 2-hop STRAIGHT pass-through, zero
         borderline gap, zero L-turn -- the simplest possible multi-hop case): title says WL=442.7.
         The overlap-midpoint chain formula (this script's `guide_point`, matching routing.cpp
         exactly) gives 157.552 -- a 2.8x difference on the simplest possible case, so this is not a
         rounding or edge-case issue.
       - path_idx4 (BLK02->BLK07, direct 1-hop abut): title says WL=0.0, confirming the *only* piece
         of guiding-point behavior this script's formula gets right is the zero-WL direct-abut case
         (QA A31).
     The decoded polyline vertices for the multi-hop paths do not sit at either endpoint rect's own
     boundary-overlap midpoint, and are NOT explained by any of: overlap-midpoint (this script's
     current formula), source/target block-centroid projection, or bounding-box HPWL (tested and
     ruled out -- see the diagnostic trace this task's implementer ran). The pattern looks like a
     jointly-optimized shortest-Manhattan-path-through-a-corridor-of-successive-boundaries
     computation (a "taut string"/funnel-style algorithm over the whole hop sequence at once, not an
     independent per-hop choice), which is a genuinely different and more involved algorithm than
     what routing.cpp implements today -- and, notably, since routing.cpp's `exactScore` uses this
     same guidePoint() formula LIVE (not just in this offline validator), the solver's real
     wirelength may be systematically off from the true contest metric for any path with 2+ hops.
     This is a substantive, separate finding outside a "byte-verify + remap constants" task's scope
     to fix by guessing; flagged for a follow-up deep-reasoner investigation rather than guessed at
     here.
  4. KNOWN GAP, narrower: the beta_tail term. Among the 5 alpha_test cases, only case1 has R > theta
     (0.05384... > 0.05; the other 4 all have R well under theta, so their overflow_penalty reduces
     to the pure-linear beta_lin*OutlineArea*R term, which matches report.json's overflow_penalty to
     float precision on all 4 -- that exact match is what proves the linear term and R's own
     definition are exactly right). For case1, the LOCKED tail formula
     (beta_tail*OutlineArea*max(0, R-theta), using the SAME global R) computed here is
     beta_tail*OutlineArea*(R-theta) = ~45705, giving overflow_penalty=8456726.22 against
     report.json's 8537517.71 -- short by ~80791 (~1% of overflow_penalty, ~0.3% of total cost,
     enough to push case1 just outside the 1e-3 cost gate). Investigated and RULED OUT: (a) any
     tail dependence on PER-CHANNEL overflow/capacity ratios (case0's CH6 has a per-channel ratio of
     31.5, vastly over theta, yet case0's overflow_penalty matches EXACTLY using the pure global-R
     linear term alone -- so the tail cannot be a function of any per-channel ratio, only of the
     global R); (b) capacity-weighted-average and max-per-channel-excess variants of the tail (both
     tried numerically, both overshoot case1 by a large margin -- see this task's diagnostic trace).
     With only ONE case exercising R > theta, there is exactly one equation and not enough
     independent data to uniquely solve for the true tail functional form without guessing -- and
     the task's formula is explicitly LOCKED ("these are DECISIONS, not suggestions"), so this script
     implements it exactly as specified rather than substitute a guessed replacement. Net effect:
     4/5 cases match COST to float precision; case1 is short by rel_err ~3.14e-3 (just over the 1e-3
     gate), isolated entirely to this one tail term.

Read from report.json: total_wirelength only (the WL fallback above), used solely as an input to
the independently-verified Cost formula for the pass/fail gate -- never blended into or presented as
an "independently recomputed" quantity. (All 5 alpha_test cases have ft_area_excess==0 and
edge_penalty==0, so the FT and edge-penalty code paths are implemented per spec but not exercised by
this validation set; edge_penalty is not modeled here at all, consistent with the locked cost
formula, which omits it.)

Usage:
    python3 tools/official_cost.py
        Validates all 5 alpha_test cases (docs/official/alpha_test/cade0002/case{0..4}/{output.cfg,
        report.json} against test/2026 ICCAD_case.case{0..4}.csv) and prints a pred-vs-report table.

    python3 tools/official_cost.py --cfg PATH.cfg --csv PATH.csv [--report PATH.json]
        Scores a single cfg/csv pair ad hoc; if --report is given, also compares against it.
"""

import argparse
import csv
import json
import math
import os
import sys

KEPS = 1e-6

# src/model.hpp kBetaLin/kBetaTail/kOverflowRateThreshold/kBetaFt.
BETA_LIN = 20.0
BETA_TAIL = 1.5
THETA = 0.05
BETA_FT = 3.0

DEFAULT_CASES = ["case0", "case1", "case2", "case3", "case4"]
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


# ---------------------------------------------------------------------------
# CSV parsing (mirrors src/io.cpp's parseProblem: alpha + per-block AREA/TYPE/FT CONVERSION rates).
# Connection-matrix nets are NOT needed for cost recomputation -- the cfg's own PATH lines already
# encode the routed nets/paths -- but the matrix sum is parsed too for an optional conn_check-style
# cross-reference against report.json.
# ---------------------------------------------------------------------------

def parse_double(s, fallback=0.0):
    t = s.strip()
    if not t:
        return fallback
    if t.endswith('%'):
        t = t[:-1]
    try:
        return float(t)
    except ValueError:
        return fallback


def has_percent(s):
    return '%' in s


def parse_type(s):
    t = s.upper()
    if 'EDGE' in t:
        return 'EDGE'
    if 'MACRO' in t or 'HARD' in t:
        return 'MACRO'
    return 'SOFT'


def parse_csv_problem(path):
    """Returns dict: alpha, blocks {name: {area, type, ft_rates}}, matrix_sum (informational)."""
    with open(path, 'r', encoding='utf-8-sig', newline='') as f:
        rows = [[cell.strip() for cell in row] for row in csv.reader(f)]

    alpha = 1.0
    blocks = {}
    block_order = []
    in_block_table = False
    for row in rows:
        if not row or row[0] == '':
            if not row:
                continue
        if not row:
            continue
        if row[0] == 'BLOCK':
            in_block_table = True
            continue
        if row[0] in ('OUTLINE', 'CONN MATRIX'):
            in_block_table = False
        if in_block_table and row[0].startswith('BLK'):
            name = row[0]
            area = parse_double(row[1]) if len(row) > 1 else 0.0
            btype = parse_type(row[5]) if len(row) > 5 else 'SOFT'
            ft_start = 7
            if len(row) > 7 and row[7].strip() != '' and not has_percent(row[7]):
                ft_start = 8
            ft_rates = []
            i = ft_start
            while i < len(row) and len(ft_rates) < 4:
                if row[i].strip() != '':
                    ft_rates.append(parse_double(row[i]) / 100.0)
                i += 1
            while len(ft_rates) < 4:
                ft_rates.append(0.2)
            blocks[name] = {'area': area, 'type': btype, 'ft_rates': ft_rates}
            block_order.append(name)

    for row in rows:
        if not row:
            continue
        if row[0].strip() == 'α' and len(row) >= 2:  # "α"
            alpha = parse_double(row[1], 1.0)

    # Optional informational cross-check: ON CHIP INTERFACE CONNECTION matrix sum (like report.json's
    # conn_check.matrix_sum). Not used in the cost computation itself.
    matrix_sum = 0.0
    for i, row in enumerate(rows):
        if row and 'ON CHIP' in row[0] and i + 1 < len(rows):
            header = [c.strip() for c in rows[i + 1]]
            for r in range(i + 2, len(rows)):
                if not rows[r] or not rows[r][0].startswith('BLK'):
                    break
                for c in range(1, min(len(rows[r]), len(header))):
                    matrix_sum += max(0.0, parse_double(rows[r][c]))
            break

    return {'alpha': alpha, 'blocks': blocks, 'block_order': block_order, 'matrix_sum': matrix_sum}


# ---------------------------------------------------------------------------
# CFG parsing: OUTLINE / BLOCK / CHANNEL / PATH sections (src/io.cpp's writeCfg format).
# ---------------------------------------------------------------------------

def parse_cfg(path):
    outline_w = outline_h = 0.0
    blocks = {}      # name -> (x, y, w, h)
    channels = {}    # name -> (x, y, w, h), insertion order preserved
    paths = []       # list of (nets, [(obj, edge), ...])
    section = None

    with open(path, 'r', encoding='utf-8') as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue
            tokens = line.split()
            head = tokens[0]
            if head == 'END':
                section = None
            elif head == 'OUTLINE':
                outline_w = float(tokens[1])
                outline_h = float(tokens[2])
            elif head == 'BLOCK' and len(tokens) == 1:
                section = 'BLOCK'
            elif head == 'CHANNEL' and len(tokens) == 1:
                section = 'CHANNEL'
            elif head == 'PATH' and len(tokens) == 1:
                section = 'PATH'
            elif section == 'PATH' and head == 'PATH':
                nets = int(round(float(tokens[1])))
                rest = tokens[2:]
                pairs = [(rest[j], int(rest[j + 1])) for j in range(0, len(rest), 2)]
                paths.append((nets, pairs))
            elif section == 'BLOCK':
                name = tokens[0]
                x, y, w, h = (float(t) for t in tokens[1:5])
                blocks[name] = (x, y, w, h)
            elif section == 'CHANNEL':
                name = tokens[0]
                x, y, w, h = (float(t) for t in tokens[1:5])
                channels[name] = (x, y, w, h)

    return {
        'outline_w': outline_w, 'outline_h': outline_h,
        'blocks': blocks, 'channels': channels, 'paths': paths,
    }


# ---------------------------------------------------------------------------
# Geometry replay -- mirrors routing.cpp's guidePoint/commitRoutePath exactly.
# ---------------------------------------------------------------------------

def rect_right(r):
    return r[0] + r[2]


def rect_top(r):
    return r[1] + r[3]


def guide_point(a, b, edge):
    """Guiding point for a hop a->b whose PATH-token edge label (edgeFromTo(a,b)) is `edge`.
    Branch/coordinate choice matches routing.cpp's guidePoint exactly (see module docstring point
    1) but is driven by the given label instead of a coordinate-equality epsilon test, since cfg
    text is rounded to 3 decimals and "touching" rects can differ by ~1e-3 after that round-trip."""
    if edge == 3:      # a.right() == b.x: a is left, b is right.
        x = rect_right(a)
        lo, hi = max(a[1], b[1]), min(rect_top(a), rect_top(b))
        return (x, (lo + hi) * 0.5)
    if edge == 1:       # a.x == b.right(): b is left, a is right.
        x = rect_right(b)
        lo, hi = max(a[1], b[1]), min(rect_top(a), rect_top(b))
        return (x, (lo + hi) * 0.5)
    if edge == 2:       # a.top() == b.y: a is below, b is above.
        y = rect_top(a)
        lo, hi = max(a[0], b[0]), min(rect_right(a), rect_right(b))
        return ((lo + hi) * 0.5, y)
    # edge == 4: a.y == b.top(): b is below, a is above.
    y = rect_top(b)
    lo, hi = max(a[0], b[0]), min(rect_right(a), rect_right(b))
    return ((lo + hi) * 0.5, y)


def manhattan(ax, ay, bx, by):
    return abs(ax - bx) + abs(ay - by)


def node_path_from_pairs(pairs):
    """PATH tokens are (object, edge) steps, 2 per hop (from-endpoint, to-endpoint) -- see
    routing.cpp's commitRoutePath step-emission loop. Recover the underlying node sequence: the
    first step's object, then every ODD-indexed step's object (the "to" endpoint of each hop)."""
    objs = [p[0] for p in pairs]
    if not objs:
        return []
    node_path = [objs[0]]
    for i in range(1, len(objs), 2):
        node_path.append(objs[i])
    return node_path


def round2(v):
    """C++ std::round (half away from zero) at 2 decimals, matching ft.hpp's round2 exactly --
    Python's builtin round() uses banker's rounding, which can differ at x.xx5 boundaries."""
    scaled = v * 100.0
    if scaled >= 0:
        return math.floor(scaled + 0.5) / 100.0
    return -math.floor(-scaled + 0.5) / 100.0


def ft_tier_rate(ft_rates, nets):
    """Exact port of ft.hpp's ftTierRate."""
    if not ft_rates:
        return 0.0
    if nets <= 3000.0:
        return ft_rates[0]
    if nets <= 6000.0:
        return ft_rates[1] if len(ft_rates) > 1 else ft_rates[-1]
    if nets <= 9000.0:
        return ft_rates[2] if len(ft_rates) > 2 else ft_rates[-1]
    return ft_rates[3] if len(ft_rates) > 3 else ft_rates[-1]


def ft_extend(nets, rate):
    """Exact port of ft.hpp's ftExtend."""
    return math.ceil((nets / 25.0) * rate * 0.5)


def ft_required_area(base_area, extend):
    """Exact port of ft.hpp's ftRequiredArea."""
    side = math.sqrt(max(1.0, base_area)) + extend
    return round2(side * side)


# ---------------------------------------------------------------------------
# Core cost computation.
# ---------------------------------------------------------------------------

def compute_cost(cfg_path, csv_path, wl_override=None):
    """Computes every Cost component independently from cfg_path/csv_path.

    `wl_override`, when given (a float), is substituted for the independently-estimated
    total_wirelength when computing alpha_times_wl/cost -- this is the sanctioned fallback for the
    KNOWN GAP in guide-point geometry (module note 3): pass report.json's own total_wirelength here
    to validate the OTHER cost terms (which independently match report.json exactly) without that
    known gap contaminating the pass/fail gate. The independently-estimated WL is still computed and
    returned (as `total_wirelength_estimate`) regardless, for diagnostic visibility into how close
    the geometric replay gets.
    """
    cfg = parse_cfg(cfg_path)
    csvp = parse_csv_problem(csv_path)

    blocks = cfg['blocks']
    channels = cfg['channels']

    def resolve_rect(name):
        if name in blocks:
            return blocks[name]
        if name in channels:
            return channels[name]
        raise KeyError(f"PATH references unknown object '{name}' (not in cfg BLOCK/CHANNEL)")

    # total_wirelength_estimate: independent geometric replay, per-hop overlap-midpoint guide points
    # (matches routing.cpp's guidePoint exactly). KNOWN to NOT match the official checker's true
    # guide-point placement for multi-hop paths -- see module note 3 -- kept as a diagnostic, not
    # used for the cost gate when wl_override is supplied.
    total_wirelength_estimate = 0.0
    channel_h_usage = {name: 0.0 for name in channels}
    channel_v_usage = {name: 0.0 for name in channels}
    soft_feedthrough_nets = {}

    for nets, pairs in cfg['paths']:
        if len(pairs) < 2:
            continue
        num_hops = len(pairs) // 2
        node_path = node_path_from_pairs(pairs)

        # One guide point per hop, from the hop's OWN edge label (pairs[2i] is the "from" endpoint
        # (obj, edge=edgeFromTo(from,to))) -- see guide_point's docstring / module note 1. This chain
        # is the total_wirelength_estimate diagnostic (module note 3: known NOT to match the checker
        # exactly for multi-hop paths).
        guides = [guide_point(resolve_rect(pairs[2 * i][0]), resolve_rect(pairs[2 * i + 1][0]), pairs[2 * i][1])
                  for i in range(num_hops)]
        wl = 0.0
        for i in range(len(guides) - 1):
            wl += manhattan(guides[i][0], guides[i][1], guides[i + 1][0], guides[i + 1][1]) * nets
        total_wirelength_estimate += wl

        # Channel usage: every (object, edge) occurrence naming a channel contributes `nets`,
        # independently per hop-touch -- see module note 2 (NOT a guide-point-delta classification).
        for obj, edge in pairs:
            if obj in channels:
                if edge in (1, 3):
                    channel_h_usage[obj] += nets
                else:
                    channel_v_usage[obj] += nets

        # FT feedthrough: soft blocks visited as PATH intermediates (not source/target endpoints).
        for i in range(1, len(node_path) - 1):
            name = node_path[i]
            if name in blocks and csvp['blocks'].get(name, {}).get('type') == 'SOFT':
                soft_feedthrough_nets[name] = soft_feedthrough_nets.get(name, 0.0) + nets

    # Channel overflow (checker/nets units) + raw capacity, exactly mirroring routing.cpp's
    # computeOverflowAndMismatch's new channel_overflow_nets/channel_capacity_total accrual.
    channel_overflow_nets = 0.0
    channel_capacity_total = 0.0
    for name, (_x, _y, w, h) in channels.items():
        cap_h = 25.0 * h
        cap_v = 25.0 * w
        channel_capacity_total += cap_h + cap_v
        hu = channel_h_usage.get(name, 0.0)
        vu = channel_v_usage.get(name, 0.0)
        if hu > cap_h:
            channel_overflow_nets += hu - cap_h
        if vu > cap_v:
            channel_overflow_nets += vu - cap_v

    # FT area excess: replay which soft blocks carried feedthrough nets, price via ft.hpp's formula
    # against the CSV's nominal AREA column (ft_base_area semantics) and the cfg's placed w*h.
    ft_area_excess = 0.0
    for name, nets in soft_feedthrough_nets.items():
        if nets <= 0.0:
            continue
        base_area = csvp['blocks'].get(name, {}).get('area', 0.0)
        ft_rates = csvp['blocks'].get(name, {}).get('ft_rates', [])
        rate = ft_tier_rate(ft_rates, nets)
        extend = ft_extend(nets, rate)
        required_area = ft_required_area(base_area, extend)
        _x, _y, w, h = blocks[name]
        actual_area = round2(w * h)
        if actual_area + KEPS < required_area:
            ft_area_excess += required_area - actual_area

    outline_area = cfg['outline_w'] * cfg['outline_h']
    alpha = csvp['alpha']
    # wl_used: the fallback (report.json's own WL) when supplied, else the independent (approximate)
    # estimate -- see compute_cost's docstring and module note 3.
    wl_used = wl_override if wl_override is not None else total_wirelength_estimate
    R = channel_overflow_nets / channel_capacity_total if channel_capacity_total > 0.0 else 0.0
    overflow_penalty = BETA_LIN * outline_area * R + BETA_TAIL * outline_area * max(0.0, R - THETA)
    ft_penalty = BETA_FT * ft_area_excess
    cost = outline_area + alpha * wl_used + overflow_penalty + ft_penalty

    return {
        'outline_area': outline_area,
        'alpha': alpha,
        'total_wirelength_estimate': total_wirelength_estimate,
        'total_wirelength_used': wl_used,
        'wl_is_fallback': wl_override is not None,
        'alpha_times_wl': alpha * wl_used,
        'channel_overflow_nets': channel_overflow_nets,
        'channel_capacity_total': channel_capacity_total,
        'overflow_rate': R,
        'overflow_penalty': overflow_penalty,
        'ft_area_excess': ft_area_excess,
        'ft_penalty': ft_penalty,
        'cost': cost,
        'matrix_sum': csvp['matrix_sum'],
        'path_sum': sum(nets for nets, _ in cfg['paths']),
    }


# ---------------------------------------------------------------------------
# Report.json comparison + CLI.
# ---------------------------------------------------------------------------

def load_report(path):
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def rel_err(pred, ref):
    denom = max(1.0, abs(ref))
    return abs(pred - ref) / denom


def print_row(label, pred, ref, width_label=24):
    err = rel_err(pred, ref)
    status = 'PASS' if err <= 1e-3 else 'FAIL'
    print(f"  {label:<{width_label}} pred={pred:>18.4f}  report={ref:>18.4f}  rel_err={err:>10.2e}  {status}")


def run_case(case_dir, csv_path, label):
    cfg_path = os.path.join(case_dir, 'output.cfg')
    report_path = os.path.join(case_dir, 'report.json')
    report = load_report(report_path)
    cb = report.get('cost_breakdown', {})
    ref_wl = cb.get('total_wirelength', report.get('total_wirelength'))

    # wl_override=ref_wl: sanctioned fallback (module note 3) -- every OTHER term below is still
    # independently recomputed from cfg_path/csv_path alone.
    pred = compute_cost(cfg_path, csv_path, wl_override=ref_wl)

    print(f"[{label}] cfg={cfg_path}")
    print(f"  csv={csv_path}")
    ref_outline_area = cb.get('outline_area', report.get('outline_area_after_ft'))
    ref_alpha_wl = cb.get('alpha_times_wl')
    ref_overflow_total = cb.get('channel_overflow_total')
    ref_capacity_total = cb.get('channel_total_capacity')
    ref_overflow_penalty = cb.get('overflow_penalty')
    ref_ft_excess = cb.get('ft_area_excess')
    ref_ft_penalty = cb.get('ft_penalty')
    ref_cost = cb.get('cost', report.get('cost'))

    print_row('outline_area', pred['outline_area'], ref_outline_area)
    print(f"  {'total_wirelength (est)':<24} pred={pred['total_wirelength_estimate']:>18.4f}  report={ref_wl:>18.4f}"
          f"  rel_err={rel_err(pred['total_wirelength_estimate'], ref_wl):>10.2e}  DIAGNOSTIC ONLY (see module note 3 -- known gap, not gated)")
    print(f"  {'total_wirelength (used)':<24} pred={pred['total_wirelength_used']:>18.4f}  report={ref_wl:>18.4f}"
          f"  {'<- fallback: read from report.json, see module note 3' if pred['wl_is_fallback'] else ''}")
    if ref_alpha_wl is not None:
        print_row('alpha_times_wl', pred['alpha_times_wl'], ref_alpha_wl)
    print_row('channel_overflow_nets', pred['channel_overflow_nets'], ref_overflow_total)
    print_row('channel_capacity_total', pred['channel_capacity_total'], ref_capacity_total)
    print_row('overflow_penalty', pred['overflow_penalty'], ref_overflow_penalty)
    print_row('ft_area_excess', pred['ft_area_excess'], ref_ft_excess if ref_ft_excess is not None else 0.0)
    if ref_ft_penalty is not None:
        print_row('ft_penalty', pred['ft_penalty'], ref_ft_penalty)
    print_row('COST (using WL fallback)', pred['cost'], ref_cost)
    conn_check = report.get('conn_check', {})
    ref_matrix_sum = conn_check.get('matrix_sum')
    ref_path_sum = conn_check.get('path_sum')
    if ref_matrix_sum is not None:
        print(f"  {'matrix_sum (info)':<24} pred={pred['matrix_sum']:>18.4f}  report={ref_matrix_sum:>18.4f}")
    if ref_path_sum is not None:
        print(f"  {'path_sum (info)':<24} pred={pred['path_sum']:>18.4f}  report={ref_path_sum:>18.4f}")
    print()

    return rel_err(pred['cost'], ref_cost) <= 1e-3, pred, ref_cost


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cfg', help='Path to a single output.cfg to score ad hoc.')
    parser.add_argument('--csv', help='Path to the matching input .csv (required with --cfg).')
    parser.add_argument('--report', help='Optional report.json to compare the ad hoc score against.')
    parser.add_argument('--alpha-root', default=os.path.join(REPO_ROOT, 'docs', 'official', 'alpha_test', 'cade0002'),
                         help='Root of the case0..case4 official alpha_test directories (default sweep mode).')
    parser.add_argument('--csv-root', default=REPO_ROOT,
                         help='Root under which "test/2026 ICCAD_case.caseN.csv" is found (default sweep mode).')
    args = parser.parse_args()

    if args.cfg:
        if not args.csv:
            parser.error('--cfg requires --csv')
        wl_override = None
        report = None
        if args.report:
            report = load_report(args.report)
            wl_override = report.get('cost_breakdown', {}).get('total_wirelength', report.get('total_wirelength'))
        pred = compute_cost(args.cfg, args.csv, wl_override=wl_override)
        print(json.dumps(pred, indent=2))
        if report is not None:
            ref_cost = report.get('cost_breakdown', {}).get('cost', report.get('cost'))
            print_row('COST', pred['cost'], ref_cost)
            sys.exit(0 if rel_err(pred['cost'], ref_cost) <= 1e-3 else 1)
        sys.exit(0)

    # Default: validate all 5 alpha_test cases.
    all_pass = True
    results = []
    for case in DEFAULT_CASES:
        case_dir = os.path.join(args.alpha_root, case)
        csv_path = os.path.join(args.csv_root, 'test', f'2026 ICCAD_case.{case}.csv')
        if not os.path.isdir(case_dir) or not os.path.isfile(csv_path):
            print(f"[{case}] SKIP (missing {case_dir} or {csv_path})")
            all_pass = False
            continue
        ok, pred, ref_cost = run_case(case_dir, csv_path, case)
        results.append((case, ok, pred['cost'], ref_cost))
        all_pass = all_pass and ok

    print('=' * 78)
    print(f"{'case':<8}{'pred cost':>20}{'report cost':>20}{'rel_err':>14}{'status':>10}")
    for case, ok, pred_cost, ref_cost in results:
        err = rel_err(pred_cost, ref_cost)
        print(f"{case:<8}{pred_cost:>20.4f}{ref_cost:>20.4f}{err:>14.2e}{'PASS' if ok else 'FAIL':>10}")
    print('=' * 78)
    n_pass = sum(1 for _, ok, _, _ in results if ok)
    if n_pass == len(DEFAULT_CASES) and len(results) == len(DEFAULT_CASES):
        print('ALL PASS')
    else:
        print(f"{n_pass}/{len(DEFAULT_CASES)} PASS. total_wirelength uses the report.json fallback "
              f"(module note 3); any remaining FAIL is expected to be case1's isolated beta_tail-term "
              f"gap (module note 4) unless a different case/component is failing above -- check the "
              f"per-case table for which row(s) actually diverge before assuming it's the known gap.")
    sys.exit(0 if all_pass and len(results) == len(DEFAULT_CASES) else 1)


if __name__ == '__main__':
    main()
