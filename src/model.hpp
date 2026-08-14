#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace efgr {

constexpr double kEps = 1e-6;

// NOTE (Beta Task 3 reconciliation): the C1 milestone's official overflow-rate coefficients
// (kBetaLin/kBetaTail/kOverflowRateThreshold/kBetaFt) live in route_cost.hpp, NOT here. The stash
// originally placed them at this efgr scope, but src/cfg_score.cpp (the locked golden re-scorer,
// committed after the stash was cut) independently defines its OWN kBetaLin/kBetaTail in an anonymous
// namespace, and cfg_score.cpp includes model.hpp -> the two collide (ambiguous name). Relocating the
// solver-side coefficients to route_cost.hpp -- which cfg_score.cpp does NOT include, and which
// already hosts the sibling routing cost weights kOverflowAreaWeight/kFtMismatchWeight -- keeps the
// oracle byte-untouched while eval.cpp (which includes route_cost.hpp) still sees them unqualified.

enum class BlockType { Edge, Macro, Soft };

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    double right() const { return x + w; }
    double top() const { return y + h; }
    double cx() const { return x + w * 0.5; }
    double cy() const { return y + h * 0.5; }
};

struct Block {
    std::string name;
    double area = 0.0;
    double ft_base_area = 0.0;  // original input area; FT required-area base (never inflated)
    double width = 0.0;
    double height = 0.0;
    double ar_min = 1.0;
    double ar_max = 1.0;
    BlockType type = BlockType::Soft;
    std::vector<std::string> locations;
    std::vector<double> ft_rates;
    int port_edge = 0;
    std::vector<int> port_edges;
};

struct Bundle {
    int source = -1;
    int target = -1;
    int nets = 0;
};

struct Problem {
    std::vector<Block> blocks;
    std::map<std::string, int> block_index;
    std::vector<Bundle> bundles;
    double outline_w = 0.0;
    double outline_h = 0.0;
    double alpha = 1.0;
    // B4.1 (route-pricing portfolio, b4-report.md): solver-internal knob selecting which router
    // pricing routeProblem's cost_driven=true path uses -- true = B4's official per-edge marginal
    // pricing (routing.cpp's default), false = the pre-B4 (HEAD 207d08c) 1e6-area-denominated
    // pricing, byte-identical to that history. NEVER parsed from the input CSV (io.cpp does not
    // touch this field; every parsed Problem keeps the default true) -- it exists purely so
    // search.cpp's runPricingPortfolio can run the SAME problem under both pricings on two Problem
    // copies and fold the results, restoring the per-case Pareto floor a changed search trajectory
    // can otherwise miss (design b4-design.md's own R4/R6 risk).
    bool route_pricing_official = true;
    // B5b (abutment-credit cheap-score upgrade, .superpowers/sdd/b5-design.md §3): solver-internal
    // knob enabling cheapScore's bundle-weighted abutment credit -- a ramp (eval.cpp's
    // abutmentCredit) that discounts the WL proxy and the route-aware congestion proxy toward zero
    // as a bundle's source/target rects approach "positiveTouch" abutment (routing.cpp:17-23's
    // adjacency test), so the cheap tier's SA gradient can find the A31 zero-WL/zero-demand cliff by
    // local moves instead of bottoming out at the un-credited center-manhattan floor. Same precedent
    // as route_pricing_official above: NEVER parsed from the input CSV (io.cpp does not touch this
    // field; every parsed Problem keeps the default false) -- exists purely so search.cpp's
    // runPricingPortfolio can run the credit as its own registry-variant dimension (SearchVariant.
    // abutment_credit) and fold the results, the same flag-gated-variant pattern B5a's own STOP
    // (b5a-report.md) established as the hard lesson: false (default) reproduces cheapScore's
    // pre-B5b arithmetic BYTE-IDENTICALLY -- the credit multiplier is gated by an `if`, never merely
    // multiplied by 1.0.
    bool abutment_credit = false;
    // F-FT (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md §6, Codex finding #1):
    // solver-internal knob selecting which FT-hop pricing routing.cpp's cost_driven=true OFFICIAL
    // branch uses -- false (default) is the CONSERVATIVE per-block ftMismatchDelta pricing
    // (route_cost.hpp), byte-identical to pre-F-FT history; true switches to the EXACT marginal of the
    // official V5 AGGREGATE FT penalty (kBetaFt*max(0, ft_area_sum-OutlineArea), route_cost.hpp's
    // roundCostOfficial) via ftRequiredDelta, so an FT hop that keeps the aggregate under the outline
    // area prices as free instead of being avoided at the (officially inflated) per-block mismatch
    // rate. This is Codex's adversarial-review finding #1 on the V5 port: the router priced FT hops
    // conservatively (per-block) while the scorer it targets charges by aggregate slack, so officially
    // -free FT shortcuts were avoided. NEVER parsed from the input CSV (io.cpp does not touch this
    // field; every parsed Problem keeps the default false) -- exists purely so search.cpp's
    // runPricingPortfolio can run this as its own registry-variant dimension (SearchVariant.
    // ft_pricing_aggressive) and fold the results, the same flag-gated-variant pattern B5b/B5c
    // established: false reproduces today's per-block pricing BYTE-IDENTICALLY. Conservative pricing
    // is retained as the FLOOR across every other leg per the Q4' outline_after_ft ambiguity
    // (qa-draft Q4') -- the aggregate's exact semantics (Sum base+delta vs Sum delta,
    // outline_after_ft) are not fully pinned, so a leg that never turns this on stays the safety net.
    bool ft_pricing_aggressive = false;
    // G4 (edge-LOCATION soft-anchor exploratory lever, docs/research/2026-07-28-alpha-v5-cost-
    // recalculation-analysis.md section 1 item 4 / section 6): solver-internal knob that treats an
    // edge block's LOCATION mismatch as the OFFICIAL checker does -- a PENALTY (kGammaEdge=1.0/um of
    // minimum translation distance, route_cost.hpp's edgeLocationViolationDistance, charged in
    // eval.cpp's exact tier) rather than validate.cpp's hard FAIL -- so the search may trade a small,
    // gamma-priced LOCATION violation for a larger alpha*WL saving. false (default) reproduces every
    // pre-G4 code path byte-identically: legalizeEdgeBlocks/placeEdge keep hard-anchoring edge blocks
    // to a satisfying LOCATION and validate.cpp keeps hard-failing any miss. NEVER parsed from the
    // input CSV (io.cpp does not touch this field) -- exists purely so main.cpp's hidden
    // --edge-soft-anchor measurement flag can opt a run into this exploration, the same flag-gated-
    // variant precedent as route_pricing_official/abutment_credit/ft_pricing_aggressive above.
    // CAVEAT: the official per-violation DISTANCE metric itself has never been observed nonzero on
    // any alpha-test case -- edgeLocationViolationDistance is a best-guess reproduction, not a
    // verified one (see its own doc comment, route_cost.hpp).
    bool edge_soft_anchor = false;
    // L6 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md): solver-internal
    // knob that merges every unordered {source,target} CONN-matrix pair into a single Bundle (nets
    // summed, direction normalized to min(source,target)->max(source,target)) via io.hpp's
    // mergeUndirectedBundles -- reflecting the official checker's own undirected CONN_MISMATCH rule
    // (PATH(A->B)+PATH(B->A) compared against m[A][B]+m[B][A], "direction distribution unconstrained",
    // reference.html:459-460/641-643) instead of routing each matrix entry as its own directed bundle.
    // false (default) reproduces every pre-L6 code path byte-identically: parseProblem's per-entry
    // directed bundles pass straight through unmerged. NEVER parsed from the input CSV (io.cpp's
    // parseProblem itself is untouched by L6) -- exists purely so main.cpp sets it, applied as a
    // POST-PARSE transform (not a parseProblem signature change, to avoid touching parseProblem's
    // other direct callers). Originally an opt-in-only measurement flag (--undirected-bundles); as of
    // P0b (docs/research/2026-08-14-final-levers-verdicts-and-0811-intel.md §3.3/§5 item 2,
    // .superpowers/sdd/p0b-brief.md) main.cpp ALSO sets this true automatically -- absent both
    // --undirected-bundles and the new --no-undirected-bundles force-off flag -- whenever
    // io.hpp's bundlesValueSymmetric(problem.bundles) is true (the L6 auto-rule), same
    // flag-gated-variant precedent as route_pricing_official/abutment_credit/ft_pricing_aggressive/
    // edge_soft_anchor above.
    bool undirected_bundles = false;
    // L2 (edge-block true un-pinning / "edge-free" mode, docs/research/2026-07-30-final-levers-
    // design.md sections L2.1-L2.8): solver-internal knob that treats every EDGE block as a free
    // MACRO -- fixed dimensions, but placed BY the SP/B* topology instead of being anchored to a
    // LOCATION-satisfying border slot by placeEdge/legalizeEdgeBlocks. Consumed via this header's
    // isAnchoredEdge(problem, i) predicate below, which every placement_engine.cpp/search.cpp/
    // respace.cpp site that used to hard-anchor on isEdge(blocks[i].type) now calls instead: OFF
    // (default) makes isAnchoredEdge degenerate to isEdge(...) exactly, so every one of those sites
    // reproduces its pre-L2 behavior byte-identically. NEVER parsed from the input CSV (io.cpp does
    // not touch this field) -- exists purely so main.cpp's hidden --edge-free measurement flag can
    // opt a run into the un-pinning (which also forces edge_soft_anchor=true, reusing its gamma
    // penalty -- moved from the exact tier to the cheap tier by this same lever, see eval.cpp's
    // cheapScore -- and validate.cpp's warning-level LOCATION-mismatch downgrade instead of a hard
    // FAIL), the same flag-gated-variant precedent as route_pricing_official/abutment_credit/
    // ft_pricing_aggressive/edge_soft_anchor/undirected_bundles above.
    bool edge_free = false;
    // L2a (ft_area_sum outline rung, case5-design.md §3.1/§4-L2a, case5-batch1-brief.md): when set,
    // outlineSweep (search.cpp) adds one extra rung sized so outline_area ~= the tight rung's own
    // ROUTED ft_area_sum (RoutingResult.ft_area_sum, routing.cpp's computeOverflowAndMismatch) --
    // the outline family the champion's feedthrough-dominant regime targets (§3.1) that neither the
    // margin ladder nor the pad ladder can reach. Adjudicated by the same outlineSweepBetter gate as
    // every other rung (Pareto-safe by construction). false (default) reproduces every pre-L2a
    // outlineSweep call byte-identically: no extra rung is ever tried. NEVER parsed from the input
    // CSV (io.cpp does not touch this field) -- exists purely so main.cpp's hidden --ft-area-rung
    // measurement flag can opt a run into it, the same flag-gated-variant precedent as
    // route_pricing_official/abutment_credit/ft_pricing_aggressive/edge_soft_anchor/
    // undirected_bundles/edge_free above.
    bool ft_area_rung = false;
    // L3a (overflow-gated pad ladder, case5-design.md §4-L3a, case5-batch1-brief.md): when set,
    // outlineSweep's 21-rung integer-um pad ladder (kPads above `outlineSweep`, search.cpp) only
    // runs when the tight rung's own routed result did NOT already clear overflow-free
    // (open_routes==0 && channel_overflow_nets==0) -- when it did, the pads are a pure capacity-
    // denominator play (officialOverflowPenalty's R, eval.cpp) against a penalty that is already 0,
    // so they can only inflate outline_area. false (default) keeps the pad loop unconditional
    // (today's behavior, gated only by `include_pads`) -- byte-identical to every pre-L3a
    // outlineSweep call. NEVER parsed from the input CSV -- exists purely so main.cpp's hidden
    // --overflow-gated-pads measurement flag can opt a run into it, same flag-gated-variant
    // precedent as ft_area_rung above.
    bool overflow_gated_pads = false;
    // L1 (cheap-tier objective realignment, case5-design.md §2.4/§4-L1,
    // .superpowers/sdd/case5-l1-brief.md): solver-internal knob replacing cheapScore's v1 route-
    // aware proxy (routeAwareProxyPenalty: a per-pair FT-bbox loop ~107x the entire real objective
    // on case5, plus a congestion term that measures identically zero there, plus a centre-to-centre
    // WL proxy blind to abutment) with scorer-unit surrogates of what the exact tier actually charges
    // -- L1a's aggregate FT term (mirroring routing.cpp:594-604/eval.cpp:247), L1b's boundary-gap WL
    // proxy (zero at abutment, matching route_geometry.hpp:93-94), and L1c's cut-line sweep that
    // turns unroutable channel demand into estimated feedthrough load instead of a flat penalty
    // (feedthrough has no capacity limit in routeProblem, routing.cpp:94-98). See eval.cpp's
    // routeAwareProxyPenaltyV2 for the implementation and the exact cancellation argument with
    // routeAndScoreAt. false (default) reproduces every pre-L1 cheapScore/routeAndScoreAt code path
    // byte-identically: the v2 branches (an `if`, never a multiply-by-one) never execute. NEVER
    // parsed from the input CSV (io.cpp does not touch this field) -- exists purely so main.cpp's
    // hidden --cheap-proxy-v2 measurement flag can opt a run into it, same flag-gated-variant
    // precedent as route_pricing_official/abutment_credit/ft_pricing_aggressive/edge_soft_anchor/
    // undirected_bundles/edge_free/ft_area_rung/overflow_gated_pads above.
    bool cheap_proxy_v2 = false;
};

struct Placement {
    std::vector<Rect> rects;
    std::vector<double> aspect;
    double outline_w = 0.0;
    double outline_h = 0.0;
};

struct Channel {
    std::string name;
    Rect rect;
    double usage = 0.0;
    double horizontal_usage = 0.0;
    double vertical_usage = 0.0;
};

struct RouteStep {
    std::string object;
    int edge = 1;
};

struct Route {
    int source = -1;
    int target = -1;
    int nets = 0;
    std::vector<int> node_path;
    std::vector<RouteStep> steps;
    double wirelength = 0.0;
    bool open = false;
};

struct RoutingResult {
    std::vector<Channel> channels;
    std::vector<Route> routes;
    std::vector<double> soft_feedthrough_nets;
    double total_wirelength = 0.0;
    double channel_overflow = 0.0;
    double feedthrough_mismatch = 0.0;
    // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section 1.4): sum, over
    // ALL problem.blocks, of the FT-converted area -- soft blocks with crossing nets contribute
    // ftRequiredArea(base, ftExtend(nets, tier rate)) (nets==0 degenerates to `base`); every other
    // block (edge/macro, and soft blocks with zero crossing nets) contributes its declared CSV area.
    // Populated by computeOverflowAndMismatch (routing.cpp). Official V5 FTPenalty = kBetaFt *
    // max(0, ft_area_sum - OutlineArea) -- an AGGREGATE slack across every block's FT-converted area,
    // NOT the per-block feedthrough_mismatch sum below (which stays, unchanged, as respace/search
    // telemetry -- it is not what the official checker charges).
    double ft_area_sum = 0.0;
    // C1 milestone: checker-unit (nets / nets-capacity) overflow accrual, RAW 25*h / 25*w capacity
    // (no max(1.0, ·) clamp -- the official checker capacitates on the true channel dimension, e.g.
    // case0 CH6 capacity_h = 25*1.231 = 30.775). Populated by computeOverflowAndMismatch alongside
    // the pre-existing area-denominated channel_overflow field above (that field is unchanged and
    // still drives respace.cpp/search.cpp comparisons; these two are additive, scorer-only inputs
    // for eval.cpp's global overflow-rate penalty).
    double channel_overflow_nets = 0.0;
    double channel_capacity_total = 0.0;
    int open_routes = 0;
    // P2 Task 5.1 (design p2-design.md §9 T5, controller amendment: comparison gate). Telemetry only
    // -- true iff routeProblem(..., cost_driven=true) returned the legacy two-pass result because it
    // won (or tied) the lexicographic (open_routes, cost) comparison gate. Never read by scoring or
    // caching logic (eval.cpp/search.cpp); exists so T7's sweep can report how often the gate saved us.
    // Always false when cost_driven=false (legacy path never sets it).
    bool used_fallback = false;
};

struct Score {
    double value = std::numeric_limits<double>::infinity();
    double outline_area = 0.0;
    double wirelength = 0.0;
    double penalty = 0.0;
};

inline bool isSoft(BlockType type) { return type == BlockType::Soft; }
inline bool isEdge(BlockType type) { return type == BlockType::Edge; }
inline bool isMacro(BlockType type) { return type == BlockType::Macro; }

// L2 (edge-free mode, docs/research/2026-07-30-final-levers-design.md §L2.3a): true when block `i`
// is pinned to the outline border and NOT free geometry -- i.e. an EDGE-type block whose position
// placeEdge/legalizeEdgeBlocks/commitOutlineAt still hard-anchor to a LOCATION-satisfying slot.
// Flag OFF (problem.edge_free's own default) makes this exactly isEdge(p.blocks[i].type), so every
// call site that substitutes isEdge(problem.blocks[i].type) -> isAnchoredEdge(problem, i) reproduces
// its pre-L2 behavior byte-identically. When edge_free is true, every EDGE block reads as NOT
// anchored -- free to enter the SP/B* topology like a MACRO -- at every one of those sites.
inline bool isAnchoredEdge(const Problem& p, std::size_t i) {
    return isEdge(p.blocks[i].type) && !p.edge_free;
}

inline bool overlaps(const Rect& a, const Rect& b) {
    return a.x + kEps < b.right() && b.x + kEps < a.right() &&
           a.y + kEps < b.top() && b.y + kEps < a.top();
}

inline double overlapArea(const Rect& a, const Rect& b) {
    double ox = std::min(a.right(), b.right()) - std::max(a.x, b.x);
    double oy = std::min(a.top(), b.top()) - std::max(a.y, b.y);
    if (ox <= 0.0 || oy <= 0.0) return 0.0;
    return ox * oy;
}

inline double manhattan(double ax, double ay, double bx, double by) {
    return std::abs(ax - bx) + std::abs(ay - by);
}

inline std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

}  // namespace efgr
