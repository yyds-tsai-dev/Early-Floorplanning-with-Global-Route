// D3 (snap-to-neighbor soft resize move, .superpowers/sdd/levers-batch2-briefs-20260817.md Brief
// 1): regression coverage for snapResizeAspect's pure geometry (algebra, AR-end clamps, the -1.0
// "no snap" sentinel across hard/edge/zero-gap/degenerate inputs, the outline fallback) plus a
// flag-ON engine-level smoke that drives 200 real mutate() calls on case5 with
// problem.snap_resize_move=true and checks isValidState + soft-aspect bounds + integer soft rects
// hold throughout. Links src/placement_engine.cpp (snapResizeAspect/mutateSnapResize/the mutate()
// gates) + src/io.cpp (parseProblem, case5 fixture only) -- see run_snap_resize_regressions.sh.
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;

Block makeSoftBlock(const std::string& name, double area, double ar_min, double ar_max) {
    Block b;
    b.name = name;
    b.area = area;
    b.ft_base_area = area;
    b.ar_min = ar_min;
    b.ar_max = ar_max;
    b.type = BlockType::Soft;
    return b;
}

Block makeFixedBlock(const std::string& name, double w, double h, BlockType type) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = type;
    return b;
}

// ---- (a) algebra on a hand-derivable 2-block fixture ---------------------------------------------

// S (soft, area=200, ar in [0.25,4], starts as a 10x20 rect at x=[0,10], y=[0,20]) with a facing
// neighbour N (macro, 5x20 at x=[30,35], y=[0,20], same y-band) directly to the right. No other
// blocks. target_w = N.x - S.x = 30 - 0 = 30 (S itself sits at x=0). ar = target_w^2/area =
// 900/200 = 4.5, clamped to ar_max=4.0.
void testAlgebraTwoBlockFixture() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 200.0, 0.25, 4.0), makeFixedBlock("N", 5.0, 20.0, BlockType::Macro)};
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 20.0}, Rect{30.0, 0.0, 5.0, 20.0}};
    pl.aspect = {0.5, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double ar = snapResizeAspect(p, pl, 0, /*width_axis=*/true);
    assert(ar > 0.0);
    assert(std::abs(ar - 4.0) < kTol);  // 900/200=4.5 clamps to ar_max=4.0 -- see testArEndClamp below too
    std::cout << "testAlgebraTwoBlockFixture ok (ar=" << ar << ")\n";
}

// Same shape, but N moved far enough away that the unclamped algebra lands INSIDE [ar_min,ar_max]
// so this test genuinely exercises the raw formula, not just the clamp. N at x=[40,45] ->
// target_w=40, ar=1600/200=8.0 -- still clamps; use N at x=[16,21] -> target_w=16, ar=256/200=1.28,
// inside [0.25,4.0].
void testAlgebraUnclamped() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 200.0, 0.25, 4.0), makeFixedBlock("N", 5.0, 20.0, BlockType::Macro)};
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 20.0}, Rect{16.0, 0.0, 5.0, 20.0}};
    pl.aspect = {0.5, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double ar = snapResizeAspect(p, pl, 0, /*width_axis=*/true);
    assert(ar > 0.0);
    assert(std::abs(ar - 1.28) < kTol);
    std::cout << "testAlgebraUnclamped ok (ar=" << ar << ")\n";
}

// ---- (b) AR-end clamps, both directions -----------------------------------------------------------

// Width axis growth only ever INCREASES ar (ar=target_w^2/area, target_w>current w) -- so the only
// reachable clamp end is ar_max. Large gap forces the raw ar far past ar_max.
void testArEndClampMax() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 100.0, 0.5, 2.0), makeFixedBlock("N", 5.0, 20.0, BlockType::Macro)};
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}, Rect{100.0, 0.0, 5.0, 20.0}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double ar = snapResizeAspect(p, pl, 0, /*width_axis=*/true);
    assert(ar > 0.0);
    assert(std::abs(ar - 2.0) < kTol);  // clamps exactly to ar_max
    std::cout << "testArEndClampMax ok (ar=" << ar << ")\n";
}

// Height axis growth only ever DECREASES ar (ar=area/target_h^2, target_h>current h) -- so the
// only reachable clamp end is ar_min. Large gap forces the raw ar far below ar_min.
void testArEndClampMin() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 100.0, 0.5, 2.0), makeFixedBlock("N", 20.0, 5.0, BlockType::Macro)};
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}, Rect{0.0, 100.0, 20.0, 5.0}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double ar = snapResizeAspect(p, pl, 0, /*width_axis=*/false);
    assert(ar > 0.0);
    assert(std::abs(ar - 0.5) < kTol);  // clamps exactly to ar_min
    std::cout << "testArEndClampMin ok (ar=" << ar << ")\n";
}

// ---- (c) -1.0 sentinel: hard / edge / zero-gap / degenerate ---------------------------------------

void testMinusOneForHardBlock() {
    Problem p;
    p.blocks = {makeFixedBlock("H", 10.0, 10.0, BlockType::Macro)};
    p.outline_w = 100.0;
    p.outline_h = 100.0;
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}};
    pl.aspect = {1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    assert(snapResizeAspect(p, pl, 0, true) == -1.0);
    assert(snapResizeAspect(p, pl, 0, false) == -1.0);
    std::cout << "testMinusOneForHardBlock ok\n";
}

void testMinusOneForEdgeBlock() {
    Problem p;
    p.blocks = {makeFixedBlock("E", 10.0, 10.0, BlockType::Edge)};
    p.outline_w = 100.0;
    p.outline_h = 100.0;
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}};
    pl.aspect = {1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    assert(snapResizeAspect(p, pl, 0, true) == -1.0);
    std::cout << "testMinusOneForEdgeBlock ok\n";
}

void testMinusOneForZeroGap() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 100.0, 0.5, 2.0), makeFixedBlock("N", 5.0, 20.0, BlockType::Macro)};
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;
    Placement pl;
    // N sits flush against S's right edge -- no growth room.
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}, Rect{10.0, 0.0, 5.0, 20.0}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    assert(snapResizeAspect(p, pl, 0, true) == -1.0);
    std::cout << "testMinusOneForZeroGap ok\n";
}

void testMinusOneForDegenerate() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 0.0, 0.5, 2.0)};  // degenerate: zero declared area
    p.outline_w = 100.0;
    p.outline_h = 100.0;
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 0.0, 0.0}};  // degenerate rect too
    pl.aspect = {1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    assert(snapResizeAspect(p, pl, 0, true) == -1.0);

    // Out-of-range index.
    assert(snapResizeAspect(p, pl, 7, true) == -1.0);
    assert(snapResizeAspect(p, pl, -1, true) == -1.0);
    std::cout << "testMinusOneForDegenerate ok\n";
}

// ---- (d) outline fallback: no facing neighbour -----------------------------------------------------

void testOutlineFallbackWidth() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 100.0, 0.5, 2.0)};
    p.outline_w = 100.0;
    p.outline_h = 1000.0;
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}};
    pl.aspect = {1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    // target_w = outline_w - r.x = 100 - 0 = 100 -> ar = 100^2/100 = 100, clamps to ar_max=2.0.
    double ar = snapResizeAspect(p, pl, 0, true);
    assert(std::abs(ar - 2.0) < kTol);
    std::cout << "testOutlineFallbackWidth ok (ar=" << ar << ")\n";
}

void testOutlineFallbackHeight() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 100.0, 0.5, 2.0)};
    p.outline_w = 1000.0;
    p.outline_h = 100.0;
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 10.0}};
    pl.aspect = {1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    // target_h = outline_h - r.y = 100 - 0 = 100 -> ar = 100/100^2 = 0.01, clamps to ar_min=0.5.
    double ar = snapResizeAspect(p, pl, 0, false);
    assert(std::abs(ar - 0.5) < kTol);
    std::cout << "testOutlineFallbackHeight ok (ar=" << ar << ")\n";
}

// ---- (f) determinism --------------------------------------------------------------------------------

void testDeterminism() {
    Problem p;
    p.blocks = {makeSoftBlock("S", 200.0, 0.25, 4.0), makeFixedBlock("N", 5.0, 20.0, BlockType::Macro)};
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 10.0, 20.0}, Rect{16.0, 0.0, 5.0, 20.0}};
    pl.aspect = {0.5, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    double a1 = snapResizeAspect(p, pl, 0, true);
    double a2 = snapResizeAspect(p, pl, 0, true);
    assert(a1 == a2);
    std::cout << "testDeterminism ok\n";
}

// ---- (e) 200 flag-ON mutations on case5: isValidState + AR bounds + integer soft rects ------------

void runFlagOnSmoke(const std::string& engine_name) {
    Problem p = parseProblem("test/E_testcase_20260811/testcase_upload_20260712/2026 ICCAD_case.case5.csv");
    p.snap_resize_move = true;
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine(engine_name);
    std::mt19937 rng(20260817);
    PlacementState state = engine->initialState(p, rng);
    for (int i = 0; i < 200; ++i) {
        engine->mutate(p, state, rng);
        assert(engine->isValidState(p, state));
    }
    Placement pl = engine->decode(p, state);
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        if (!isSoft(p.blocks[i].type)) continue;
        double ar = pl.aspect.size() > i ? pl.aspect[i] : 1.0;
        assert(ar >= p.blocks[i].ar_min - kTol && ar <= p.blocks[i].ar_max + kTol);
        const Rect& r = pl.rects[i];
        assert(std::abs(r.w - std::round(r.w)) < 1e-3);
        assert(std::abs(r.h - std::round(r.h)) < 1e-3);
    }
    std::cout << "runFlagOnSmoke(" << engine_name << ") ok\n";
}

void test200FlagOnMutationsCase5() {
    runFlagOnSmoke("sp");
    runFlagOnSmoke("bstar");
}

}  // namespace

int main() {
    testAlgebraTwoBlockFixture();
    testAlgebraUnclamped();
    testArEndClampMax();
    testArEndClampMin();
    testMinusOneForHardBlock();
    testMinusOneForEdgeBlock();
    testMinusOneForZeroGap();
    testMinusOneForDegenerate();
    testOutlineFallbackWidth();
    testOutlineFallbackHeight();
    testDeterminism();
    test200FlagOnMutationsCase5();
    std::cout << "snap-resize (D3) regressions passed\n";
    return 0;
}
