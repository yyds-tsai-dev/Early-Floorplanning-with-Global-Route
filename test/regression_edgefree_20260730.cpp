// L2 (edge-block true un-pinning / "edge-free" mode, docs/research/2026-07-30-final-levers-design.md
// sections L2.1-L2.8): regression coverage for Problem.edge_free / isAnchoredEdge and their
// downstream effects -- topology insertion (movableBlocks), decode leaving a freed edge block off
// its declared LOCATION, shrink-wrap degenerating to a tight bbox with no rect movement, the
// cheap-tier gamma gradient (this lever's core fix over G4, which only ever charged gamma on the
// exact tier), the exact-tier gamma double-charge removal (the (d) arithmetic move), validate's
// existing warning-mode downgrade, and the G4 move gate's edge-free suppression. Each test's own
// header comment names which design section L2.8 row (E1..E8) it covers. movableBlocks/
// placementLegalAt/commitOutlineAt/mutateEdgeAnchorShift all have internal linkage in
// placement_engine.cpp, so E1/E2/E3/E4/E8 observe them INDIRECTLY through the public
// PlacementEngine/shrinkWrapOutline surface -- the only way a separate translation unit can reach
// them, and the same surface real callers (search.cpp) use.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../src/eval.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/route_cost.hpp"
#include "../src/validate.hpp"

namespace {
using namespace efgr;

Block makeBlock(const std::string& name, BlockType type, double w, double h,
                std::vector<std::string> locs = {}) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.ft_base_area = w * h;
    b.ar_min = 1.0;
    b.ar_max = 1.0;
    b.type = type;
    b.locations = std::move(locs);
    return b;
}

// ---- E1: movableBlocks contents (design sec L2.8) -------------------------------------------------
// movableBlocks() has internal linkage -- observed via PlacementEngine::initialState, which seeds
// PlacementState.sequence_pos from it verbatim (SequencePairEngine::initialState, placement_engine.
// cpp).

void testE1MovableBlocksContents() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"BL"}));
    problem.blocks.push_back(makeBlock("M1", BlockType::Macro, 100.0, 100.0));
    problem.blocks.push_back(makeBlock("E2", BlockType::Edge, 40.0, 40.0, {"TR"}));
    problem.blocks.push_back(makeBlock("M2", BlockType::Macro, 80.0, 80.0));

    std::mt19937 rng(1);
    std::unique_ptr<PlacementEngine> sp = makePlacementEngine("sp");

    problem.edge_free = false;
    PlacementState anchored = sp->initialState(problem, rng);
    std::vector<int> anchored_ids = anchored.sequence_pos;
    std::sort(anchored_ids.begin(), anchored_ids.end());
    assert((anchored_ids == std::vector<int>{1, 3}));  // only the two MACRO blocks

    problem.edge_free = true;
    PlacementState freed = sp->initialState(problem, rng);
    std::vector<int> freed_ids = freed.sequence_pos;
    std::sort(freed_ids.begin(), freed_ids.end());
    assert((freed_ids == std::vector<int>{0, 1, 2, 3}));  // every block, edge included

    std::cout << "testE1MovableBlocksContents ok\n";
}

// ---- E2: decode really leaves a freed edge block off-LOCATION (design sec L2.8) -------------------
// Fixture: outline far larger than the two blocks' combined footprint, one T-side ("TM") edge block
// made the SHORTER of the two movable blocks, so the shrink-wrapped bbox height is driven by the
// other (taller) block -- guaranteeing the edge block's top edge cannot coincidentally land flush
// with the final outline_h by construction alone (with only 1 movable block, a lone rect's top
// trivially equals the shrink-wrapped outline_h, which would make this test vacuous).

void testE2DecodeLeavesBlockOffLocation() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    problem.edge_free = true;
    problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"TM"}));
    problem.blocks.push_back(makeBlock("M1", BlockType::Macro, 100.0, 200.0));

    std::mt19937 rng(3);
    std::unique_ptr<PlacementEngine> bstar = makePlacementEngine("bstar");
    // initialState's chain (root=nodes[0], nodes[1]=left-child of root) is deterministic for a
    // 2-node tree -- rng is required by the interface but not consumed for this shape.
    PlacementState state = bstar->initialState(problem, rng);
    Placement placement = bstar->decode(problem, state);

    assert(!edgeLocSatisfied(placement.rects[0], placement.outline_w, placement.outline_h, "TM"));

    bool contained = true;
    for (const auto& r : placement.rects) {
        if (r.x < -kEps || r.y < -kEps || r.right() > placement.outline_w + kEps ||
            r.top() > placement.outline_h + kEps) {
            contained = false;
        }
    }
    bool no_overlap = !overlaps(placement.rects[0], placement.rects[1]);
    assert(contained && no_overlap);  // placementLegalAt's own two checks, re-derived (internal linkage)
    std::cout << "testE2DecodeLeavesBlockOffLocation ok (rect0 top=" << placement.rects[0].top()
              << " outline_h=" << placement.outline_h << ")\n";
}

// ---- E3 + E4: shrink-wrap degenerates to a tight bbox, commitOutlineAt stops erasing (design
// sec L2.8) ------------------------------------------------------------------------------------
// Both properties are only observable through shrinkWrapOutline (commitOutlineAt itself has
// internal linkage) -- a single call proves both: the returned (w,h) equals the tight bbox of a
// HAND-BUILT, already-legal placement (E3), and every rect is untouched afterward, only outline_w/h
// stamped (E4).

void testE3E4ShrinkWrapDegeneratesNoErasure() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    problem.edge_free = true;
    problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"BL"}));
    problem.blocks.push_back(makeBlock("M1", BlockType::Macro, 100.0, 100.0));

    Placement placement;
    placement.rects.push_back(Rect{500.0, 500.0, 50.0, 50.0});  // off "BL" -- irrelevant to this test
    placement.rects.push_back(Rect{0.0, 0.0, 100.0, 100.0});
    const Rect before0 = placement.rects[0];
    const Rect before1 = placement.rects[1];

    OutlineFit fit = shrinkWrapOutline(problem, placement);

    assert(fit.legal);
    assert(fit.w == 550.0);  // ceil(tight bbox) -- already integer
    assert(fit.h == 550.0);
    assert(placement.outline_w == 550.0 && placement.outline_h == 550.0);  // stamped

    // E4: rects byte-identical to their pre-call values -- nothing erased/reset.
    assert(placement.rects[0].x == before0.x && placement.rects[0].y == before0.y &&
           placement.rects[0].w == before0.w && placement.rects[0].h == before0.h);
    assert(placement.rects[1].x == before1.x && placement.rects[1].y == before1.y &&
           placement.rects[1].w == before1.w && placement.rects[1].h == before1.h);
    std::cout << "testE3E4ShrinkWrapDegeneratesNoErasure ok (w=" << fit.w << " h=" << fit.h << ")\n";
}

// ---- E5: cheap-tier gamma gradient exists (design sec L2.8) ---------------------------------------
// THE key test for this lever -- G4's own flaw was that cheapScore had zero gamma term, so the SA's
// cheap tier (the vast majority of iterations) never felt the LOCATION penalty at all. Two
// placements differ ONLY in one edge block's position; zero bundles collapse wire_proxy and
// routeAwareProxyPenalty to 0 identically on both sides, and both placements stamp the SAME
// (outline_w, outline_h), so outline_area matches too -- isolating cheapScore's value delta to
// exactly the new gamma term.

void testE5CheapTierGammaGradientExists() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    problem.edge_soft_anchor = true;
    problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"BL"}));

    Placement satisfied;
    satisfied.outline_w = 500.0;
    satisfied.outline_h = 500.0;
    satisfied.rects.push_back(Rect{0.0, 0.0, 50.0, 50.0});  // flush at x=0,y=0 -> satisfies "BL"

    Placement violated = satisfied;
    violated.rects[0] = Rect{200.0, 200.0, 50.0, 50.0};  // off both axes

    Score score_ok = cheapScore(problem, satisfied);
    Score score_bad = cheapScore(problem, violated);
    assert(score_ok.outline_area == score_bad.outline_area);  // proves the "other terms cancel" claim
    assert(score_ok.wirelength == score_bad.wirelength);

    double dist_ok = edgeLocationViolationDistance(satisfied.rects[0], satisfied.outline_w,
                                                    satisfied.outline_h, problem.blocks[0].locations);
    double dist_bad = edgeLocationViolationDistance(violated.rects[0], violated.outline_w,
                                                     violated.outline_h, problem.blocks[0].locations);
    assert(dist_ok == 0.0);
    assert(dist_bad > 0.0);

    double expected_delta = kGammaEdge * (dist_bad - dist_ok);
    double actual_delta = score_bad.value - score_ok.value;
    assert(std::abs(actual_delta - expected_delta) < 1e-9);
    std::cout << "testE5CheapTierGammaGradientExists ok (delta=" << actual_delta << ")\n";
}

// ---- E6: exact-tier gamma not double-charged (design sec L2.8) ------------------------------------
// Proves the (d) arithmetic move is equivalence-preserving, not just non-doubled: reassemble
// exactScore's expected value from the SAME RoutingResult fields it produced (never hand-predicting
// routing internals) plus an independently-computed gamma term, and require exact agreement. Zero
// bundles make open_routes==0 and total_wirelength==0 trivially true, so the corrective-grow branch
// never fires and (w,h) stays exactly the stamped (500,500).

void testE6ExactTierGammaNotDoubleCharged() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    problem.alpha = 1.0;
    problem.edge_soft_anchor = true;
    problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"BL"}));
    problem.blocks.push_back(makeBlock("M1", BlockType::Macro, 100.0, 100.0));

    Placement placement;
    placement.outline_w = 500.0;
    placement.outline_h = 500.0;
    placement.rects.push_back(Rect{300.0, 300.0, 50.0, 50.0});  // off "BL" -> nonzero gamma
    placement.rects.push_back(Rect{0.0, 0.0, 100.0, 100.0});

    RoutingResult routing;
    Score score = exactScore(problem, placement, &routing);
    assert(routing.open_routes == 0);

    double A = placement.outline_w * placement.outline_h;
    double R = routing.channel_capacity_total > 0.0
             ? routing.channel_overflow_nets / routing.channel_capacity_total : 0.0;
    double overflow_penalty = officialOverflowPenalty(R, A);
    double ft_penalty = kBetaFt * std::max(0.0, routing.ft_area_sum - A);
    double gamma = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) continue;
        gamma += edgeLocationViolationDistance(placement.rects[i], placement.outline_w,
                                               placement.outline_h, problem.blocks[i].locations);
    }
    assert(gamma > 0.0);

    double expected = A + problem.alpha * routing.total_wirelength + overflow_penalty + ft_penalty +
                      kGammaEdge * gamma;
    assert(std::abs(score.value - expected) < 1e-6);
    std::cout << "testE6ExactTierGammaNotDoubleCharged ok (gamma=" << gamma << " score=" << score.value
              << ")\n";
}

// ---- E7: validate soft mode (design sec L2.8) ------------------------------------------------------
// Not a new code path (G4 already added this to validate.cpp) -- confirms --edge-free's reuse of it
// via edge_soft_anchor still holds: vr.ok stays true and the warning message is present.

void testE7ValidateSoftModeWarning() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    problem.edge_soft_anchor = true;
    problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"BL"}));

    Placement placement;
    placement.rects.push_back(Rect{300.0, 300.0, 50.0, 50.0});  // off "BL"

    RoutingResult routing;  // no routes, no bundles -> CONN coverage check trivially satisfied

    ValidationResult vr = validateSolution(problem, placement, routing);
    assert(vr.ok);
    bool found = false;
    for (const auto& msg : vr.messages) {
        if (msg.find("warning: edge location violation") != std::string::npos) found = true;
    }
    assert(found);
    std::cout << "testE7ValidateSoftModeWarning ok\n";
}

// ---- E8: the G4 move is disabled under edge-free (design sec L2.8) --------------------------------
// Two independent pieces of evidence for "mutate() never calls mutateEdgeAnchorShift when edge_free
// is true, even though --edge-free also sets edge_soft_anchor":
//   Part 1: edge_anchor_dx/dy (the sole fields mutateEdgeAnchorShift ever writes) stay all-zero over
//           many mutate() calls.
//   Part 2: RNG-consumption-count proof. On a Problem with NO edge blocks (so edge_free's OTHER
//           effect -- movableBlocks' size -- cannot confound the comparison), (edge_soft_anchor=
//           false, edge_free=false) and (edge_soft_anchor=true, edge_free=true) both make the gate
//           `problem.edge_soft_anchor && !problem.edge_free` false, so both must fall through to the
//           identical move_portfolio_v1 body and consume IDENTICAL rng draws for the same seed --
//           not just reach a zero net effect by coincidence.

void testE8G4MoveDisabledUnderEdgeFree() {
    {
        Problem problem;
        problem.outline_w = 1000.0;
        problem.outline_h = 1000.0;
        problem.edge_soft_anchor = true;
        problem.edge_free = true;
        problem.blocks.push_back(makeBlock("E1", BlockType::Edge, 50.0, 50.0, {"BL"}));
        problem.blocks.push_back(makeBlock("M1", BlockType::Macro, 100.0, 100.0));

        std::mt19937 rng(7);
        std::unique_ptr<PlacementEngine> sp = makePlacementEngine("sp");
        PlacementState state = sp->initialState(problem, rng);
        for (int i = 0; i < 500; ++i) sp->mutate(problem, state, rng);

        bool all_zero = true;
        for (double v : state.edge_anchor_dx) {
            if (v != 0.0) all_zero = false;
        }
        for (double v : state.edge_anchor_dy) {
            if (v != 0.0) all_zero = false;
        }
        assert(all_zero);
    }
    {
        Problem base;
        base.outline_w = 1000.0;
        base.outline_h = 1000.0;
        base.blocks.push_back(makeBlock("M1", BlockType::Macro, 100.0, 100.0));
        base.blocks.push_back(makeBlock("M2", BlockType::Macro, 80.0, 80.0));
        base.blocks.push_back(makeBlock("S1", BlockType::Soft, 60.0, 60.0));

        Problem off = base;
        off.edge_soft_anchor = false;
        off.edge_free = false;

        Problem freed = base;
        freed.edge_soft_anchor = true;
        freed.edge_free = true;

        std::mt19937 rng_off(42);
        std::mt19937 rng_freed(42);
        std::unique_ptr<PlacementEngine> sp_off = makePlacementEngine("sp");
        std::unique_ptr<PlacementEngine> sp_freed = makePlacementEngine("sp");
        // initialState does not consume rng (SP's sequence is a deterministic movableBlocks() copy),
        // so both engines' rng streams are still exactly seed-42-fresh (and still equal to each
        // other) going into the mutate loop below.
        PlacementState state_off = sp_off->initialState(off, rng_off);
        PlacementState state_freed = sp_freed->initialState(freed, rng_freed);

        for (int i = 0; i < 500; ++i) {
            sp_off->mutate(off, state_off, rng_off);
            sp_freed->mutate(freed, state_freed, rng_freed);
        }

        assert(state_off.sequence_pos == state_freed.sequence_pos);
        assert(state_off.sequence_neg == state_freed.sequence_neg);
        assert(state_off.soft_aspect == state_freed.soft_aspect);
        assert(state_off.edge_location_choice == state_freed.edge_location_choice);
        assert(rng_off == rng_freed);  // the two rng streams are still in lockstep
    }
    std::cout << "testE8G4MoveDisabledUnderEdgeFree ok\n";
}

}  // namespace

int main() {
    testE1MovableBlocksContents();
    testE2DecodeLeavesBlockOffLocation();
    testE3E4ShrinkWrapDegeneratesNoErasure();
    testE5CheapTierGammaGradientExists();
    testE6ExactTierGammaNotDoubleCharged();
    testE7ValidateSoftModeWarning();
    testE8G4MoveDisabledUnderEdgeFree();
    std::cout << "edge-free regressions passed\n";
    return 0;
}
