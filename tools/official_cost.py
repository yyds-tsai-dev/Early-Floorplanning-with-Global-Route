#!/usr/bin/env python3
"""Local replica of the official ICCAD 2026 Problem E checker cost -- Cost Function V5.

Milestone C1 acceptance gate: independently recompute the official checker's cost from a solved
``output.cfg`` (OUTLINE / BLOCK / CHANNEL / PATH sections, contest output format -- see
``src/io.cpp``'s ``writeCfg``) plus the matching input ``.csv`` (alpha, block AREA / TYPE / FT
CONVERSION rates -- see ``src/io.cpp``'s ``parseProblem``), and compare against the official
checker's own ``report.json`` output.

V5 PORT (2026-07-28): the official checker moved to "Cost Function V5" -- see
docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md (evidence:
docs/official/alpha_test/cade0002_calculation.html, "PATH Detail Cost Calculation", eval run
2026-07-19). This module mirrors tools/verify_v5_alpha.py, which is byte/example-verified EXACT
against that page, while keeping this script's own CLI/report structure. Module notes 1-4 below
predate the V5 port; notes 2-4 are now resolved/superseded by it (marked inline) and kept for
provenance -- they explain WHY the V5 rules below look the way they do.

Formula (V5; see the analysis doc above for the byte-verified derivation of each term):

    Cost = OutlineArea + alpha*WL + overflow_penalty + ft_penalty
    WL = sum over PATHs of nets * manhattan_chain_length(guiding points); guiding points are a
        `cross_gp` for every step between two different objects, and a `corner_gp` (or nothing,
        for a same-axis straight pass-through) for every step that stays on one object -- see
        `path_wl_v5`.
    overflow_penalty = beta_lin*OutlineArea*R                                           if R<=theta
                      = beta_lin*theta*OutlineArea + beta_tail*OutlineArea*sqrt(R-theta) if R>theta
    R = channel_overflow_nets / channel_total_capacity
    channel_overflow_nets = sum over channels of
        max(0, horizontal_usage - 25*h) + max(0, vertical_usage - 25*w)      (nets)
    channel_total_capacity = sum over channels of (25*h + 25*w)              (nets; RAW, unclamped)
    horizontal_usage/vertical_usage: each PATH traversal of a channel charges its nets ONCE per
        axis touched -- a straight pass-through charges 1 axis, an L-turn charges both.
    ft_penalty = beta_ft * ft_area_excess
    ft_area_excess = max(0, ft_area_sum - OutlineArea)
    ft_area_sum = sum over ALL blocks of CSV base AREA, plus (required_area - base_area) for every
        SOFT block that carried feedthrough nets as a PATH intermediate.
    beta_lin=20.0, beta_tail=1.5, theta=0.05, beta_ft=3.0 (src/model.hpp kBetaLin/kBetaTail/
    kOverflowRateThreshold/kBetaFt -- kept in sync manually; this script has no C++ dependency).

Independently RECOMPUTED by this script (validated to <=5e-6 against the V5 page's five official
example-path WL subtotals and five per-case overflow/capacity totals -- see
tools/verify_v5_alpha.py and the pred/report table this script prints):
  - OutlineArea: from the cfg's own OUTLINE line (W*H).
  - total_wirelength: the V5 guiding-point chain above, replayed per PATH from its (object, edge)
    token sequence -- see `path_wl_v5`. Previously a report.json fallback (module note 3); that
    gap is now closed, so this is independently exact rather than a diagnostic-only estimate.
  - channel_overflow_nets / channel_total_capacity: by replaying each PATH's channel touches,
    pairing each channel visit's (entry, exit) edges and charging nets once per axis touched
    (module note 2's old per-token double-charge is superseded) -- then applying raw 25*h/25*w
    capacity from the cfg's own CHANNEL rects.
  - ft_area_excess: by replaying which SOFT blocks appear as PATH intermediates (feedthrough) and
    how many nets cross them, applying the same ft.hpp formula (ftTierRate/ftExtend/
    ftRequiredArea) against each such block's CSV AREA column, then summing the aggregate margin
    across every block in the problem rather than a per-block mismatch (the official semantics,
    per the analysis doc's section 4). (Not exercised by the alpha_test set -- always 0 -- but
    implemented per spec.)

`wl_override` (an optional `compute_cost` parameter) lets a caller pin `alpha_times_wl`/`cost` to an
external WL instead of this script's own V5 estimate. It is no longer needed to route around a gap
-- there isn't one now -- so every prediction this script computes on its own defaults to
`wl_override=None` (pure V5): both the default 5-case sweep (`run_case`) and the ad hoc `--cfg` path
compute `pred` independently of report.json. (Codex #3 fix: `run_case` used to default to pinning WL
to report.json here, which leaked into `alpha_times_wl`/`cost` and made the printed "pred cost" not
pure V5.) `wl_override` remains wired through the ad hoc path's `--report` flag for a caller who
explicitly wants to pin WL to an external value; the default sweep never sets it, and instead prints
report.json's numbers alongside `pred` purely as a labeled historical (pre-V5) reference. Those
reference columns are EXPECTED to diverge from `pred` now (other than OutlineArea/matrix_sum/
path_sum), since report.json predates the V5 rule change (see the analysis doc's section 3 old-vs-V5
comparison table); this script's own `pred` values are fully independent of it.

Two things the PRE-V5 replay got right, discovered by cross-referencing case0's routing_details/
channel.demand_h_by_channel/demand_v_by_channel against a first-draft implementation that used
routing.cpp's own internal conventions verbatim (which turned out NOT to match the checker's on
both counts) -- plus two gaps that investigation left open, both now CLOSED by the V5 port above:

  1. Guide points must be derived from the PATH tokens' explicit edge label, not by re-deriving
     "does rect a touch rect b" via a coordinate-equality epsilon on the cfg's printed (3-decimal)
     coordinates. The checker itself tolerates and FLAGS such gaps as "borderline" (e.g. case0
     path_idx1 step0: BLK02.right()=1827.948 vs CH11.x=1827.947, gap=0.001) -- a tight epsilon
     (routing.cpp's internal kEps=1e-6, which is fine pre-text-round-trip) misclassifies these as
     non-touching and derives the wrong guide point. The edge label was computed by the router
     BEFORE text round-tripping and is authoritative; use it to pick the branch. STILL TRUE under
     V5 -- `cross_gp`/`corner_gp` are still driven by the PATH token's edge label; only the
     within-branch midpoint formula changed (see the Formula section above).
  2. SUPERSEDED BY V5 (see the Formula section above -- demand is now single-charge-per-axis-per-
     traversal): a channel's usage used to be accrued PER HOP-TOUCH, not per through-traversal
     direction. A straight pass-through channel (entry and exit on the same axis, e.g.
     left-in/right-out) was charged 2*nets on that axis -- confirmed exactly against case0's OLD
     report.json: CH6 carries a single 500-net straight-through path yet report.json's
     demand_h_by_channel.CH6 is 1000 (500 for the entry touch + 500 for the exit touch); CH8's
     split entry-vertical/exit-horizontal path similarly reproduced demand_h_by_channel.CH8=500 /
     demand_v_by_channel.CH8=2300 exactly. This differed from routing.cpp's OWN internal
     commitRoutePath (which prices a pass-through channel's usage ONCE per direction via the net
     entry-to-exit guide-point delta -- a documented, deliberate router-side simplification/proxy
     -- see routing.cpp's channel-hop comment in findRoutePath) -- it was specifically the OLD
     (pre-V5) checker's own double-charging convention, not something this script invented.
  3. RESOLVED BY V5 (formerly "KNOWN GAP", why total_wirelength used to fall back to report.json):
     the official checker's guiding point for a channel hop is NOT the touched boundary's overlap
     midpoint that routing.cpp's guidePoint() computes. Ground truth from the checker's own
     per-path SVG visualizations (docs/official/alpha_test/cade0002/case0/viz/path_{0..4}.html),
     which each embed a dashed <polyline> of the actual guide points used AND a title with that
     exact path's WL, showed e.g. path_idx0 (BLK02->CH8->BLK03, 900 nets, a plain 2-hop STRAIGHT
     pass-through, zero borderline gap, zero L-turn -- the simplest possible multi-hop case):
     title says WL=442.7, where the old overlap-midpoint chain formula gave 157.552 -- a 2.8x
     difference on the simplest possible case, so this was never a rounding or edge-case issue.
     The V5 page (A41 + Figure 6) answers this exactly: a cross guiding point with the
     across-border coordinate AVERAGED between both rects' near edges (not snapped to one side, as
     the old formula did), plus a corner point inserted for same-object axis turns -- see
     `cross_gp`/`corner_gp`, EXACT against all five official example-path subtotals (see
     tools/verify_v5_alpha.py). This closes the item flagged here as a follow-up deep-reasoner
     investigation; none is needed. (Since routing.cpp's `exactScore` still uses the OLD
     guidePoint() formula LIVE, the solver's own real wirelength accounting is a separate,
     still-open port -- out of scope for this script.)
  4. RESOLVED BY V5 (formerly "KNOWN GAP, narrower"): the beta_tail term. Among the 5 alpha_test
     cases, only case1 had R > theta under the OLD (double-charged) demand rule (0.05384... >
     0.05; the other 4 all had R well under theta, so their overflow_penalty reduced to the
     pure-linear beta_lin*OutlineArea*R term, which matched report.json's overflow_penalty to
     float precision on all 4 -- that exact match is what proved the linear term and R's own
     definition were exactly right even before V5). For case1, the plain-linear tail formula this
     script used to implement (beta_tail*OutlineArea*max(0, R-theta)) computed
     beta_tail*OutlineArea*(R-theta) = ~45705, giving overflow_penalty=8456726.22 against
     report.json's 8537517.71 -- short by ~80791 (~1% of overflow_penalty, ~0.3% of total cost),
     with not enough independent R>theta data points (only case1) to solve the true functional
     form without guessing. The V5 page's piecewise form -- beta_lin*theta*OutlineArea +
     beta_tail*OutlineArea*sqrt(R-theta) for R>theta -- reproduces that OLD case1 penalty
     (8,537,517.705283) to float precision (see tools/verify_v5_alpha.py section 3): the tail was
     always a sqrt, not a second linear segment. Under V5's single-charge demand rule, all 5
     alpha_test cases now fall in the pure-linear region (R well under theta), so the sqrt branch
     is unexercised by this validation set but implemented per the resolved formula
     (`overflow_penalty_v5`).

Read from report.json: nothing, for the cost computation itself -- OutlineArea, WL,
overflow_penalty, and ft_penalty are all independently recomputed under V5 (see above).
`wl_override`/`--report` remain available to pin an external WL and print a pred-vs-report.json
comparison table. All 5 alpha_test cases have ft_area_excess==0 and edge_penalty==0 either way, so
the FT and edge-penalty code paths are implemented per spec but not exercised by this validation
set; edge_penalty is not modeled here at all, consistent with the Cost formula above, which omits
it.

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
# Geometry replay -- V5 guiding points (cross_gp/corner_gp/path_wl_v5), exact port of
# tools/verify_v5_alpha.py; PATH token structure still mirrors routing.cpp's commitRoutePath.
# ---------------------------------------------------------------------------

def rect_edge(r, e):
    """Edge descriptor for guiding-point math: (axis, fixed coord, span lo, span hi). Edges are
    CCW from left: 1=left, 2=top, 3=right, 4=bottom (matches routing.cpp's/the PATH tokens' edge
    labels). Exact port of tools/verify_v5_alpha.py's R.edge for this file's (x, y, w, h) tuples."""
    x, y, w, h = r
    if e == 1:
        return ('H', x, y, y + h)
    if e == 3:
        return ('H', x + w, y, y + h)
    if e == 2:
        return ('V', y + h, x, x + w)
    if e == 4:
        return ('V', y, x, x + w)
    raise ValueError(e)


def cross_gp(ra, ea, rb, eb):
    """V5 cross guiding point for a step between two DIFFERENT objects: the midpoint of the
    overlapping border segment, with the across-border coordinate AVERAGED between both rects'
    near edges (rather than snapped to one side) -- this absorbs the <=0.001 adjacency gap the
    cfg's 3-decimal rounding can leave. Exact port of tools/verify_v5_alpha.py's cross_gp."""
    axa, ca, lo_a, hi_a = rect_edge(ra, ea)
    _, cb, lo_b, hi_b = rect_edge(rb, eb)
    mid = 0.5 * (max(lo_a, lo_b) + min(hi_a, hi_b))
    fixed = 0.5 * (ca + cb)
    return (fixed, mid) if axa == 'H' else (mid, fixed)


def corner_gp(r, e_in, e_out):
    """V5 guiding point for a step that stays on the SAME object (a channel's entry/exit edges):
    the intersection corner of the two edges, or None when they share an axis (straight
    pass-through -- no point is inserted, per A41/Figure 6). Exact port of
    tools/verify_v5_alpha.py's corner_gp."""
    ax_i, c_i, _, _ = rect_edge(r, e_in)
    ax_o, c_o, _, _ = rect_edge(r, e_out)
    if ax_i == ax_o:
        return None
    return (c_i, c_o) if ax_i == 'H' else (c_o, c_i)


def manhattan(ax, ay, bx, by):
    return abs(ax - bx) + abs(ay - by)


def path_wl_v5(pairs, resolve_rect):
    """V5 wirelength for one PATH's full (object, edge) token sequence: `cross_gp` for every step
    between two different objects, `corner_gp` (or nothing, for a same-axis straight pass-through)
    for every step that stays on one object, chained by Manhattan distance. Exact port of
    tools/verify_v5_alpha.py's path_wl_v5 (there driven off a raw dict lookup; here off this
    module's resolve_rect, so a PATH referencing an unknown object still raises a clear KeyError)."""
    gps = []
    for (na, ea), (nb, eb) in zip(pairs, pairs[1:]):
        if na == nb:
            g = corner_gp(resolve_rect(na), ea, eb)
            if g is not None:
                gps.append(g)
        else:
            gps.append(cross_gp(resolve_rect(na), ea, resolve_rect(nb), eb))
    return sum(manhattan(a[0], a[1], b[0], b[1]) for a, b in zip(gps, gps[1:]))


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


def overflow_penalty_v5(rate, outline_area):
    """V5 piecewise overflow tail: linear up to theta; beyond theta the linear term caps at theta
    and the excess gets a diminishing sqrt tail instead of more linear growth. Exact port of
    tools/verify_v5_alpha.py's overflow_penalty_v5."""
    if rate <= THETA:
        return BETA_LIN * outline_area * rate
    return BETA_LIN * THETA * outline_area + BETA_TAIL * math.sqrt(rate - THETA) * outline_area


# ---------------------------------------------------------------------------
# Core cost computation.
# ---------------------------------------------------------------------------

def compute_cost(cfg_path, csv_path, wl_override=None):
    """Computes every Cost component independently from cfg_path/csv_path (V5 rules throughout).

    `wl_override`, when given (a float), is substituted for the independently-computed
    total_wirelength when computing alpha_times_wl/cost. Under the pre-V5 script this routed around
    a known gap in guide-point geometry (module note 3); that gap is now closed -- `path_wl_v5`
    reproduces the V5 page's five official example-path subtotals EXACTLY -- so `wl_override` is
    kept only so callers (e.g. the default sweep, via --report) can still pin WL to an external
    report.json value for comparison. The independently-computed WL is always returned too (as
    `total_wirelength_estimate`), regardless of whether an override was supplied.
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

    # total_wirelength_estimate: independent V5 guide-point replay (path_wl_v5) -- EXACT against the
    # official V5 calculation page's five example-path subtotals (module note 3, resolved). Still
    # named "_estimate" for compute_cost's return-shape/CLI compatibility; only overridden by
    # wl_used below when an explicit wl_override is supplied (e.g. for report.json comparison).
    total_wirelength_estimate = 0.0
    channel_h_usage = {name: 0.0 for name in channels}
    channel_v_usage = {name: 0.0 for name in channels}
    soft_feedthrough_nets = {}

    for nets, pairs in cfg['paths']:
        if len(pairs) < 2:
            continue
        node_path = node_path_from_pairs(pairs)

        # V5 wirelength: cross_gp for every step between two different objects, corner_gp (or
        # nothing, for a same-axis straight pass-through) for every step that stays on one object
        # -- see path_wl_v5's docstring / module note 1/3.
        total_wirelength_estimate += path_wl_v5(pairs, resolve_rect) * nets

        # Channel usage (V5 single-charge-per-axis-per-traversal -- module note 2): group each
        # channel visit into its (entry, exit) edge pair and charge nets once per axis touched --
        # a straight pass-through (entry/exit same axis) charges that axis once; an L-turn charges
        # both axes once each.
        j = 0
        while j < len(pairs):
            obj, edge = pairs[j]
            if obj in channels:
                if j + 1 < len(pairs) and pairs[j + 1][0] == obj:
                    e_in, e_out = edge, pairs[j + 1][1]
                    j += 2
                else:
                    e_in, e_out = edge, edge
                    j += 1
                for axis in {'H' if ee in (1, 3) else 'V' for ee in (e_in, e_out)}:
                    if axis == 'H':
                        channel_h_usage[obj] += nets
                    else:
                        channel_v_usage[obj] += nets
            else:
                j += 1

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

    # FT area excess (V5 aggregate margin, not a per-block mismatch): sum every block's CSV base
    # area, plus the FT conversion delta (required_area - base_area, from the same ftTierRate/
    # ftExtend/ftRequiredArea replay as before) for each SOFT block that carried feedthrough nets.
    ft_area_sum = sum(info.get('area', 0.0) for info in csvp['blocks'].values())
    for name, nets in soft_feedthrough_nets.items():
        if nets <= 0.0:
            continue
        base_area = csvp['blocks'].get(name, {}).get('area', 0.0)
        ft_rates = csvp['blocks'].get(name, {}).get('ft_rates', [])
        rate = ft_tier_rate(ft_rates, nets)
        extend = ft_extend(nets, rate)
        required_area = ft_required_area(base_area, extend)
        ft_area_sum += required_area - base_area

    outline_area = cfg['outline_w'] * cfg['outline_h']
    alpha = csvp['alpha']
    # wl_used: the fallback (report.json's own WL) when supplied, else this script's own
    # independently computed V5 estimate -- see compute_cost's docstring.
    wl_used = wl_override if wl_override is not None else total_wirelength_estimate
    R = channel_overflow_nets / channel_capacity_total if channel_capacity_total > 0.0 else 0.0
    overflow_penalty = overflow_penalty_v5(R, outline_area)
    ft_area_excess = max(0.0, ft_area_sum - outline_area)
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

    # wl_override=None: pred is pure V5, independent of report.json (Codex #3 fix -- this call used
    # to pin WL to report.json's pre-V5 value here, which leaked into alpha_times_wl/cost and made
    # the printed "pred cost" not pure V5). report.json predates Cost Function V5 (see the module
    # docstring's V5 PORT note), so EVERY term below is expected to diverge from it; ref_wl etc. are
    # only ever used as printed historical-reference columns, never fed back into pred.
    pred = compute_cost(cfg_path, csv_path, wl_override=None)

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
    print(f"  {'total_wirelength (V5)':<24} pred={pred['total_wirelength_estimate']:>18.4f}  report={ref_wl:>18.4f}"
          f"  rel_err={rel_err(pred['total_wirelength_estimate'], ref_wl):>10.2e}  V5 vs PRE-V5 report.json -- expected to diverge, not gated")
    print(f"  {'total_wirelength (used)':<24} pred={pred['total_wirelength_used']:>18.4f}  report={ref_wl:>18.4f}"
          f"  {'<- pinned to report.json (pre-V5) for this comparison table only' if pred['wl_is_fallback'] else '<- pure V5 (same as above; no override)'}")
    if ref_alpha_wl is not None:
        print_row('alpha_times_wl', pred['alpha_times_wl'], ref_alpha_wl)
    print_row('channel_overflow_nets', pred['channel_overflow_nets'], ref_overflow_total)
    print_row('channel_capacity_total', pred['channel_capacity_total'], ref_capacity_total)
    print_row('overflow_penalty', pred['overflow_penalty'], ref_overflow_penalty)
    print_row('ft_area_excess', pred['ft_area_excess'], ref_ft_excess if ref_ft_excess is not None else 0.0)
    if ref_ft_penalty is not None:
        print_row('ft_penalty', pred['ft_penalty'], ref_ft_penalty)
    print_row('COST (pure V5)', pred['cost'], ref_cost)
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
        print(f"{n_pass}/{len(DEFAULT_CASES)} PASS against report.json. report.json predates Cost "
              f"Function V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md), so "
              f"FAIL rows here are EXPECTED now -- V5 changed the WL/demand/tail/ft rules, this is "
              f"not a regression in this script. To check V5 correctness itself, compare the "
              f"per-case pred wirelength/overflow/capacity printed above against the V5 calculation "
              f"page's own values, or run tools/verify_v5_alpha.py (byte-verified exact against it).")
    sys.exit(0 if all_pass and len(results) == len(DEFAULT_CASES) else 1)


if __name__ == '__main__':
    main()
