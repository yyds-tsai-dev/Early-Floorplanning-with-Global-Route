#pragma once

#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"

namespace efgr {

struct PlacementState {
    std::vector<int> sequence_pos;
    std::vector<int> sequence_neg;

    std::vector<int> nodes;
    std::vector<int> parent;
    std::vector<int> left;
    std::vector<int> right;
    int root = -1;

    std::vector<double> soft_aspect;
    std::vector<int> edge_location_choice;
    // G4 (edge-LOCATION soft-anchor exploratory lever, docs/research/2026-07-28-alpha-v5-cost-
    // recalculation-analysis.md): per-block (index-aligned, like edge_location_choice above) x/y
    // translation applied to an EDGE block's rect by placement_engine.cpp's applyEdgeSoftAnchor,
    // AFTER legalizeEdgeBlocks/shrinkWrapOutline have anchored it at a LOCATION-satisfying position
    // -- only ever written by mutateEdgeAnchorShift and only ever read when problem.edge_soft_anchor
    // is true (model.hpp), so a flag-off run never touches these fields even though every state
    // technically carries them. Empty/short vectors read as an all-zero offset (no shift), matching
    // edge_location_choice/soft_aspect's own defensive short-vector handling.
    std::vector<double> edge_anchor_dx;
    std::vector<double> edge_anchor_dy;
};

class PlacementEngine {
public:
    virtual ~PlacementEngine() = default;

    virtual std::string name() const = 0;
    virtual PlacementState initialState(const Problem& problem, std::mt19937& rng) const = 0;
    virtual PlacementState seedFromPlacement(const Problem& problem, const Placement& placement,
                                             std::mt19937& rng) const;
    virtual void mutate(const Problem& problem, PlacementState& state, std::mt19937& rng) const = 0;
    virtual Placement decode(const Problem& problem, const PlacementState& state) const = 0;
    virtual bool isValidState(const Problem& problem, const PlacementState& state) const = 0;
};

// B5c (move portfolio, .superpowers/sdd/b5-design.md §4): `move_portfolio_v2` is baked into the
// CONSTRUCTED engine instance (default false), NOT read from any per-call argument -- `mutate`'s
// signature is fixed at (problem, state, rng) across the whole codebase (search.cpp's SA loop,
// finalizeFeedthrough, every test's own PlacementEngine subclass), so threading a new search-level
// flag through it without adding a 4th parameter (touching the abstract interface and every
// override/call site) means the flag must live on the ENGINE OBJECT itself. SearchOptions.
// move_portfolio_v2 (search.hpp) is the source of truth for registry legs; runPricingPortfolio's
// runLeg/runHybridPlacementSearch construct engines via this SAME boolean so the two stay in sync --
// see search.cpp's runLeg for the one production call site. Existing callers that omit the second
// argument keep constructing a flag-off (byte-identical-mutate) engine, unchanged.
std::unique_ptr<PlacementEngine> makePlacementEngine(const std::string& name, bool move_portfolio_v2 = false);

// L4b (shape-aware shelf seed, case5-design.md §4-L4b, docs/research/2026-08-15-case5-l1-m2-
// verdict-and-m1-wall-breakdown.md §1 "basin-discovery" evidence): deterministic AR-flexible row
// packing -- non-edge blocks partitioned into `nrows` rows by descending area (greedy least-loaded,
// index tiebreak), row k's height h_k = (row area sum)/W, each SOFT member sized to that height
// within its own feasible band (aspect = w/h in [ar_min, ar_max] => h in [sqrt(A/ar_max),
// sqrt(A/ar_min)]; an infeasible h_k is clamped to the nearest band edge -- the block may poke past
// the row line, which is fine: the result is a SEED for seedFromPlacement (which only reads the
// rect ORDERING) plus stampSoftAspectFromPlacement (which reads the aspects), never a final
// placement, so neither legality nor the integer-dims rule (dimensionFor) applies to it. Hard
// macros keep their fixed CSV dims; edge blocks are left zero-sized at the origin (movableBlocks
// excludes them; decode re-anchors them from LOCATION). Rows stack bottom-to-top, members pack
// left-to-right from x=0. Pure and deterministic -- no RNG.
Placement shelfSeed(const Problem& problem, int nrows, double W);

// L4b aspect stamp (case5-design.md §4-L4b "the single highest-value line in L4"): copies each
// soft block's PLACED aspect (rect.w/rect.h, clamped to the block's [ar_min, ar_max]) into
// state.soft_aspect -- seedFromPlacement's baseState resets every soft aspect to ~1.0, silently
// discarding the seed placement's shapes; without this stamp a shelf seed decodes into squares and
// the row structure is destroyed. Applied ONLY at explicitly-seeded call sites (deep-mode shelf
// legs) -- hybrid's own internal sp->bstar re-seed keeps its historical reset-to-1.0 behavior
// (changing it would break budget-off byte-identity; a flag-gated variant of that is a separate
// future lever). Rects with nonpositive dims (edge placeholders) are skipped.
void stampSoftAspectFromPlacement(const Problem& problem, const Placement& placement, PlacementState& state);
std::vector<std::string> placementEngineNames();

// B5c (move portfolio, .superpowers/sdd/b5-design.md §4): shared primitives for the wire-median /
// abutment-targeted / greedy-reposition moves (§4.1), exposed as free functions (rather than engine
// members) so both SequencePairEngine and BStarEngine's mutate share one implementation and so each
// is unit-testable in isolation (mirrors search.hpp's rememberBounded/reservoirSample/
// buildEliteFullSet precedent, hoisted for the exact same reason). Every rng-consuming one takes
// `rng` as an explicit parameter, never a captured/static generator, so the caller's own mutate()
// stream stays the single source of randomness (design §4, R6).

// Bundle-weighted median of block `x`'s partners' centers (design §4.1 wireMedian): collects
// (partner_center, nets) over every bundle touching `x` and returns the weighted median of the
// x-coordinates and (independently) the y-coordinates -- the per-axis HPWL-optimal point for `x`
// (2023-D optimPos). Even-total-weight ties resolve to the LOWER weighted median (the smallest
// coordinate at which cumulative weight first reaches half the total) -- the standard weighted-
// median convention for 1D optimal-position placement; the design doc does not pin an alternate
// formula. Returns `x`'s own current center unchanged if `x` has no bundles (no partners to target).
std::pair<double, double> wireMedianTarget(const Problem& problem, const Placement& placement, int x);

// Roulette-wheel pick of one bundle index weighted by its net count (design §4.1
// netWeightedBundle): prefix-sum + one uniform draw over [1, total_nets]. Returns -1 if
// problem.bundles is empty.
int netWeightedBundle(const Problem& problem, std::mt19937& rng);

// argmax over movable (non-edge) blocks of a badness metric (design §4.1 worstBlock): bundle-
// weighted alpha*nets*manhattan distance to each partner, plus a congestion-proxy share mirroring
// eval.cpp's routeAwareProxyPenalty overflow term (that function has internal linkage in eval.cpp,
// out of scope for this task to touch -- see b5c-report.md for the honest scope note: the smaller
// FT/soft-block-bbox share of routeAwareProxyPenalty is NOT replicated here). Returns -1 if there
// are no movable blocks. O(bundles).
int worstBlock(const Problem& problem, const Placement& placement);

// D3 (snap-to-neighbor soft resize move, .superpowers/sdd/levers-batch2-briefs-20260817.md Brief
// 1): grow-only, slack-closing resize candidate for SOFT block `idx` along one axis -- the aspect
// ratio (clamped to [block.ar_min, block.ar_max]) that results from stretching that block's
// DECLARED area (area-preserving, mirrors dimensionFor's own w*h == area contract) up to its
// nearest facing neighbour along that axis, or to the outline edge if no neighbour faces it.
// width_axis=true grows toward +x (target_w = the smallest facing neighbour's x, or outline_w,
// minus r.x); false grows toward +y (target_h = the smallest facing neighbour's y, or outline_h,
// minus r.y) -- symmetric. "Facing" means the neighbour's rect overlaps `idx`'s rect in the
// OTHER axis's band (y-band for the width axis, x-band for the height axis) and sits at or past
// `idx`'s own far edge (kEps tolerance) -- taking the MINIMUM such coordinate keeps the grown
// block non-critical under the longest-path decode (no push-out of the neighbour). Pure function
// of (problem, placement, idx, width_axis); no RNG, no mutation of `placement`. Returns -1.0 (never
// a legal aspect ratio) when `idx` is out of range, not a SOFT block, its placed rect is
// degenerate (w or h <= 0), the block's declared area is <= 0, or there is no growth room (the
// resolved target <= the current dimension + kEps) -- callers treat -1.0 as "no snap available" and
// fall back to the existing soft-aspect move (mutateSnapResize's own fallback contract).
double snapResizeAspect(const Problem& problem, const Placement& placement, int idx, bool width_axis);

// Which side of `p` the repositioned block ends up on (design §4.2/§4.3's 4-way reinsertion table).
enum class AdjacentSide { Left, Right, Below, Above };

// SP realization (design §4.2, SequencePairEngine::mutate): removes `x` from both
// state.sequence_pos/sequence_neg and reinserts it immediately adjacent to `p` per the quadrant-
// reinsertion table (erase+insert is always a permutation of the same elements, so isValidState
// holds by construction). No-op if `x` or `p` is not present in sequence_pos (e.g. an edge block) --
// callers must confirm both are movable blocks first.
void reinsertSPAdjacent(PlacementState& state, int x, int p, AdjacentSide side);

// B*-tree realization (design §4.3, BStarEngine::mutate): a tree child slot only ever expresses
// "right-of"/"above-of" its parent, so Right/Above attach `x` as a child of `p`, while Left/Below
// realize the opposite two directions by attaching `p` as a child of `x` instead (swapping which
// node is the "mover") -- both are the SAME detach+reattach primitive, just with the two block
// arguments swapped. The mover MUST currently be a tree leaf (both children empty): detaching an
// internal node would orphan its own children, a case this primitive does not attempt to repair
// (mirrors moveLeaf's own leaf-only scope -- design §4.3 itself cites "the moveLeaf detach logic").
// Returns false (state left completely unchanged) when the resolved mover is not a leaf, is the
// tree's root, or either block is absent from state.nodes (e.g. an edge block).
// `strict`: true (the abutment-targeted move, which has already verified THIS EXACT side's PORT
// EDGE legality) accepts only the geometrically-exact child slot -- no fallback, so a false return
// means "not realizable as this exact side," letting the caller try the next side rather than
// silently landing a different, unverified adjacency. false (wire-median / greedy-reposition, which
// have no specific-side legality contract) falls back to the anchor's other slot, and finally to the
// mover's just-vacated old-parent slot (always free), so those callers succeed whenever the mover is
// a valid leaf -- mirrors moveLeaf's own free-slot-target fallback, `rng`-driven.
bool reinsertBStarChild(PlacementState& state, int x, int p, AdjacentSide side, std::mt19937& rng, bool strict);

struct OutlineFit {
    double w = 0.0;
    double h = 0.0;
    bool legal = true;
};

// Cheap: O(n) lower-bound arithmetic + thirds feasibility probe only, no edge relegalization loop.
// Does NOT mutate placement; returns the estimated (W,H) for cheapScore.
OutlineFit shrinkWrapEstimate(const Problem& problem, const Placement& placement);

// Exact: re-legalizes edge blocks against the shrunk outline via t-interpolation bisection plus
// per-axis refinement (design p1-design.md §1.2.4 amendment 2026-07-06; replaces the earlier
// linear x1.05 grow loop, which exhausted before reaching the true feasible frontier on loose
// cases). Mutates placement (edge rects re-anchored to the shrunk border + outline_w/h stamped).
// Core (non-edge) rects are never moved. Falls back to problem's outline (MAX) if no smaller
// (W,H) is legal within max_grow_steps bisection iterations -- always terminates legal.
// max_grow_steps default raised 6 -> 12 (bisection converges faster per step than linear growth,
// but the iteration cap is now shared with the coupled + two per-axis refinement passes).
OutlineFit shrinkWrapOutline(const Problem& problem, Placement& placement, int max_grow_steps = 12);

// Anchors edge blocks against EXACTLY `scoped`'s outline_w/h (no search/bisection): re-legalizes
// edge blocks against that outline via legalizeEdgeBlocks, gates on placementLegalAt, and on
// success stamps `placement.outline_w/h` with (scoped.outline_w, scoped.outline_h). Core
// (non-edge) rects are never moved. Shares its commit logic with shrinkWrapOutline (both re-reset
// edge rects via dimensionFor, legalize, and gate on placementLegalAt) but performs no bisection of
// its own -- unlike shrinkWrapOutline, which always searches DOWN from whatever ceiling it is
// given, this callable is for callers (outlineSweep, exactScore's corrective grow) that already
// picked a specific target and need it anchored AT that exact size, not re-minimized. Returns
// legal=false (placement left untouched) if that exact outline cannot host a legal edge anchoring.
OutlineFit anchorAtOutline(const Problem& scoped, Placement& placement);

}  // namespace efgr
