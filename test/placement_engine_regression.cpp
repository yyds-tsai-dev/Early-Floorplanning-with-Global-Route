#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include "../src/placement_engine.hpp"
#include "../src/search.hpp"

using namespace efgr;

namespace {

int fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

bool anyOverlap(const Placement& placement) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(placement.rects[i], placement.rects[j])) return true;
        }
    }
    return false;
}

Problem makeThreeSoftProblem() {
    Problem p;
    p.outline_w = 200.0;
    p.outline_h = 120.0;
    p.alpha = 1.0;
    p.blocks = {
        Block{"A", 400.0, 400.0, 0.0, 0.0, 0.5, 2.0, BlockType::Soft, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"B", 900.0, 900.0, 0.0, 0.0, 0.5, 2.0, BlockType::Soft, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"C", 625.0, 625.0, 0.0, 0.0, 0.5, 2.0, BlockType::Soft, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    for (std::size_t i = 0; i < p.blocks.size(); ++i) p.block_index[p.blocks[i].name] = static_cast<int>(i);
    p.bundles.push_back(Bundle{0, 2, 10});
    return p;
}

Problem makeEdgeAnchoredProblem() {
    Problem p;
    p.outline_w = 120.0;
    p.outline_h = 100.0;
    p.blocks = {
        Block{"EDGE", 100.0, 100.0, 10.0, 10.0, 1.0, 1.0, BlockType::Edge, {"TR"}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"SOFT", 400.0, 400.0, 0.0, 0.0, 1.0, 1.0, BlockType::Soft, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    p.block_index["EDGE"] = 0;
    p.block_index["SOFT"] = 1;
    return p;
}

int testSequencePairInitialDecodeRespectsOrder() {
    Problem p = makeThreeSoftProblem();
    std::mt19937 rng(7);
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("sp");
    PlacementState state = engine->initialState(p, rng);
    Placement placement = engine->decode(p, state);

    if (engine->name() != "sp") return fail("sequence-pair engine name should be sp");
    if (anyOverlap(placement)) return fail("sequence-pair initial decode should not overlap blocks");
    if (placement.rects[0].right() > placement.rects[1].x + kEps) {
        return fail("same-order sequence-pair seed should place A left of B");
    }
    return 0;
}

int testSequencePairOppositeOrderCreatesVerticalConstraint() {
    Problem p = makeThreeSoftProblem();
    std::mt19937 rng(8);
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("sp");
    PlacementState state = engine->initialState(p, rng);
    std::swap(state.sequence_neg[0], state.sequence_neg[1]);
    Placement placement = engine->decode(p, state);

    if (anyOverlap(placement)) return fail("sequence-pair vertical decode should not overlap blocks");
    if (placement.rects[0].top() > placement.rects[1].y + kEps) {
        return fail("opposite sequence order should place A below B");
    }
    return 0;
}

int testBStarInitialDecodeAndMutationValidity() {
    Problem p = makeThreeSoftProblem();
    std::mt19937 rng(11);
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    PlacementState state = engine->initialState(p, rng);
    if (!engine->isValidState(p, state)) return fail("initial B*-tree state should be valid");

    Placement placement = engine->decode(p, state);
    if (engine->name() != "bstar") return fail("B*-tree engine name should be bstar");
    if (anyOverlap(placement)) return fail("B*-tree initial decode should not overlap blocks");

    for (int i = 0; i < 100; ++i) {
        engine->mutate(p, state, rng);
        if (!engine->isValidState(p, state)) return fail("B*-tree mutation should preserve a valid tree");
    }
    return 0;
}

int testEdgeBlocksStayAnchored() {
    Problem p = makeEdgeAnchoredProblem();
    for (const char* engine_name : {"sp", "bstar"}) {
        std::mt19937 rng(19);
        std::unique_ptr<PlacementEngine> engine = makePlacementEngine(engine_name);
        PlacementState state = engine->initialState(p, rng);
        Placement placement = engine->decode(p, state);
        const Rect& edge = placement.rects[0];
        if (std::abs(edge.top() - p.outline_h) > kEps || edge.x + kEps < (2.0 * p.outline_w / 3.0)) {
            return fail(std::string(engine_name) + " should keep TR edge block on top TR segment");
        }
    }
    return 0;
}

int testHybridSearchProducesRoutedResult() {
    Problem p = makeThreeSoftProblem();
    SearchOptions options;
    options.iters = 20;
    options.seed = 5;
    options.exact_interval = 5;

    SearchResult result = runHybridPlacementSearch(p, options);
    if (result.engine_name != "hybrid") return fail("hybrid search should report hybrid engine name");
    if (result.exact_evaluations <= 0) return fail("hybrid search should run exact evaluations");
    if (result.best_placement.rects.size() != p.blocks.size()) return fail("hybrid placement should include every block");
    return 0;
}

}  // namespace

int main() {
    if (int rc = testSequencePairInitialDecodeRespectsOrder()) return rc;
    if (int rc = testSequencePairOppositeOrderCreatesVerticalConstraint()) return rc;
    if (int rc = testBStarInitialDecodeAndMutationValidity()) return rc;
    if (int rc = testEdgeBlocksStayAnchored()) return rc;
    if (int rc = testHybridSearchProducesRoutedResult()) return rc;
    std::cerr << "placement_engine_regression: ok\n";
    return 0;
}
