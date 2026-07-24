#pragma once

#include <vector>

#include "model.hpp"
#include "search.hpp"

namespace efgr {

// Bounding core-block indices (or -1 for an outline wall) for a single channel, geometrically
// re-derived from the final placement rects + the channel's own rect. Pure; no mutation.
struct ChannelBounds {
    int left = -1;
    int right = -1;
    int below = -1;
    int above = -1;
};

// Per-block-pair gap demand, addressed by directional need (design p3-design.md §1.2-1.3,
// the axis-crossing invariant): need_w charges the (left,right) horizontal-gap pair, need_h
// charges the (below,above) vertical-gap pair. Indices are -1 for an outline wall.
struct StripDemand {
    int left = -1;
    int right = -1;
    int below = -1;
    int above = -1;
    double need_w = 0.0;
    double need_h = 0.0;
};

// Re-derives, for one channel, the block indices (or -1 for a wall) bounding it on each side:
// left/right via x-edge match + y-overlap, below/above via y-edge match + x-overlap. Pure;
// reads only `problem`/`placement`; does not touch RoutingResult/Channel structs.
ChannelBounds channelBounds(const Problem& problem, const Placement& placement, const Rect& channel_rect);

// For each channel with positive directional usage, derives its bounding block pair(s) and the
// absolute gap each one demands (design p3-design.md §1.2): vertical_usage -> need_w on the
// (left,right) pair; horizontal_usage -> need_h on the (below,above) pair. Channels with zero
// usage in both directions produce no entry. Pure; no mutation of problem/placement/routing.
// NOTE: entries are one-per-channel and NOT pre-merged -- when multiple channels stack on the
// same (left,right) or (below,above) pair (design p3-design.md §1.2 "Multiple channels per
// strip"), callers MUST max-merge need_w/need_h across all entries sharing that pair before
// using them as a single edge weight; the graph works in absolute per-pair gaps, so summing or
// using an arbitrary single entry would under- or mis-size the gap.
std::vector<StripDemand> channelDemands(const Problem& problem, const Placement& placement,
                                         const RoutingResult& routing);

// Relaxes CORE (non-edge) block coordinates to satisfy the strip demands via two independent
// longest-path constraint-graph passes (design p3-design.md §1-2, decision 4; p3-task-2 review
// fix): an H-pass over x-coordinates using need_w on (left,right) pairs, then a V-pass over
// y-coordinates using need_h on (below,above) pairs -- deliberately in that order, since the
// V-pass's cross-overlap predicate reads the POST-H-PASS x-geometry, which is the real geometry
// that governs vertical collision. Node order per axis is frozen from `placement`'s CURRENT
// coordinates; the relax only ever increases a core block's coordinate (monotone widen), so
// relative order can never flip. Each block's new coordinate is the max over its OLD coordinate,
// EVERY earlier-in-frozen-order block it cross-overlaps (not just the nearest one), and EVERY
// demand pair keyed to it (regardless of cross-overlap with the demand's other endpoint) -- a
// single selected predecessor is not enough to avoid both silently dropping a demand keyed to a
// non-nearest pair and letting a tie-losing candidate's stale position get overlapped by a block
// that legitimately grew past it. Edge-type blocks are not graph nodes and are left untouched
// (re-anchored elsewhere). If the unclamped relax would push an axis's extent past
// `problem.outline_w`/`outline_h` (MAX), the added demand slack is proportionally scaled back
// (never the base non-overlap terms) so the result never exceeds MAX; this is a partial-relief
// case, not a failure. Mutates `placement` in place and always returns true (per design
// failure-mode 1): true means "relaxed within MAX," not "every demand fully satisfied" -- a
// demand can still be left partially unsatisfied by the MAX scale-back, which is a graded penalty
// evaluated elsewhere, not a hard failure this return value encodes.
bool relaxToDemand(const Problem& problem, Placement& placement, const std::vector<StripDemand>& demands);

// Single-pass demand-driven respace (design p3-design.md T3, §2.1 steps 1-5): reads the routed
// result's per-channel demand, relaxes core-block coordinates to satisfy it (`relaxToDemand`),
// re-anchors edge blocks + re-stamps the outline via `shrinkWrapOutline`, and gates the whole
// result on legality (containment, pairwise non-overlap, edge-location rules) before committing.
// Operates on a LOCAL copy of `placement`; on ANY failure (no demand, or the relaxed+re-anchored
// result is illegal) the caller's `placement` is left completely untouched and this returns false.
// On success, `placement` is overwritten with the legal, widened-where-needed result and this
// returns true. Pure aside from the documented `placement` mutation; deterministic (no RNG).
bool respaceToDemand(const Problem& problem, Placement& placement, const RoutingResult& routing);

// Outcome of the route<->respace fixed-point loop: the best-legal placement/routing/score seen
// across all iterations (including the un-respaced baseline as the fallback), plus whether that
// best is strictly better than the baseline.
struct RespaceResult {
    Placement placement;
    RoutingResult routing;
    Score score;
    bool improved = false;
};

// Route<->respace fixed point (design p3-design.md §2.6, decision 5): scores `base` via
// `exactCached` as the baseline (also the fallback if respacing never improves on it), then runs
// up to 3 iterations of respaceToDemand-on-the-current-routed-placement followed by a fresh
// exactCached re-score. Each iteration's candidate is compared against the best-so-far using the
// same open_routes==0-strictly-first-then-lower-value preference `outlineSweepBetter` already
// applies in search.cpp (replicated locally per task-4 brief -- respaceFixedPoint lives in
// respace.cpp, not search.cpp, so nothing is moved across files). Stops early when
// `respaceToDemand` returns false (no demand left, or the candidate is infeasible/illegal),
// when the just-accepted routing's channel_overflow is already <= kEps, or when respaceToDemand
// made no coordinate change (monotone-widen: further iterations could not do anything new).
// Deterministic: no RNG, and `exactCached`/`respaceToDemand` are themselves deterministic.
RespaceResult respaceFixedPoint(const Problem& problem, const Placement& base, const ExactCachedFn& exactCached);

// B7 (capacity-profit interior-gap rungs, .superpowers/sdd/b7-brief.md; research doc
// 2026-07-23 §3c): one interior gap -- a specific block-pair (or block/outline-wall pair)
// bounding an existing channel, per channelBounds -- and a candidate integer widening amount.
// `horizontal=true` widens the (left,right) pair's x-separation (grows the shared channel(s)'
// WIDTH, hence VERTICAL-usage capacity, per channelDemands' own axis-crossing invariant);
// `horizontal=false` widens the (below,above) pair's y-separation (grows HEIGHT / HORIZONTAL-
// usage capacity). `from`/`to` use channelBounds' -1-for-wall convention for `from` only -- `to`
// is always a real, non-edge block index (capacityGapCandidates below excludes any gap whose
// `to` would be the outline wall or an edge-type block, since relaxToDemand's constraint graph
// has no mechanism to move either one: a (block -> high-side-wall) demand is never read by
// relaxAxis's per-block loop, which only ever iterates CORE block targets, and an edge-type `to`
// is never a member of that same `core` node set either -- both would be silent no-ops via
// applyGapCandidate below, so they are excluded at enumeration time rather than ranked-and-
// wasted). `profit` is the analytic ranking key -- see capacityGapCandidates' own doc comment
// (respace.cpp) for the derivation; descending profit is the intended application order.
struct CapacityGapCandidate {
    int from = -1;
    int to = -1;
    bool horizontal = true;
    double delta = 0.0;
    double profit = 0.0;
};

// Enumerates every interior gap channelBounds identifies across `routing.channels` (deduped by
// (from,to,horizontal) -- one entry per distinct pair, not per channel, since several stacked
// channels can share the same bounding pair and widening it once widens all of them together),
// crossed with three integer widening amounts delta in {1,2,4} um, and ranks the result
// DESCENDING by an analytic profit estimate with explicit deterministic tie-breaks (profit desc,
// then horizontal before vertical, then `from`, then `to`, then `delta`) -- see respace.cpp for
// the full derivation. Returns an EMPTY vector whenever the current routing carries no overflow
// (`routing.channel_overflow_nets <= kEps`) or no capacity (`routing.channel_capacity_total <=
// kEps`): the profit formula is provably <= 0 for every gap in that regime (see respace.cpp),
// which is also the cheap early-out the brief's own "honest scope" note anticipates (most
// default winners already carry near-zero overflow post-B4). Pure; no mutation of
// problem/placement/routing. `placement`/`routing` MUST be a mutually consistent pair (the SAME
// call site's own exactCached-produced placement + its routing, never a placement re-derived or
// mutated after routing was computed) -- channelBounds re-derives bounds geometrically from
// `placement` alone, so a stale/mismatched pairing could mis-attribute a channel's bounding pair
// (the P3-final-review "channelBounds wrong-pair attribution under stamp/routing disagreement"
// risk research doc 2026-07-23 §8 item 5 flagged as load-bearing here) -- callers must follow the
// same discipline every other respace.cpp/search.cpp call site already does (result.best_placement
// and result.best_routing are always assigned together, from the same exactCached call).
std::vector<CapacityGapCandidate> capacityGapCandidates(const Problem& problem, const Placement& placement,
                                                        const RoutingResult& routing);

// Applies one gap candidate's widening to a COPY of `placement`, via the SAME relax/re-anchor/
// legality-gate sequence respaceToDemand uses (respace.cpp's shared applyDemandsAndLegalize) --
// builds a single-entry StripDemand whose required gap is the candidate's CURRENT geometric
// separation (re-derived directly from `placement`, not stored on the candidate) plus
// `gap.delta` (widen the EXISTING gap by delta, not "set the gap to delta" -- the latter would be
// a near-universal no-op, since almost every real gap already exceeds a few integer microns),
// runs relaxToDemand, re-anchors edge blocks + re-stamps the outline via shrinkWrapOutline, and
// gates on legality. On failure (illegal, or would exceed MAX), `placement` is left completely
// untouched and this returns false, mirroring respaceToDemand's own contract exactly.
// Deterministic (no RNG).
bool applyGapCandidate(const Problem& problem, Placement& placement, const CapacityGapCandidate& gap);

}  // namespace efgr
