#include <cmath>
#include <iostream>
#include <string>

#include "../src/floorplan.hpp"
#include "../src/io.hpp"
#include "../src/routing.hpp"

using namespace efgr;

namespace {

bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}

int fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

int testParsePortAndFtRates() {
    Problem p = parseProblem("test/E_testcase_20260608_port.csv");
    const Block& edge = p.blocks[p.block_index.at("BLK01")];
    if (edge.port_edge != 3) return fail("BLK01 PORT should parse as edge 3");
    if (edge.ft_rates.size() != 4) return fail("BLK01 should have four FT rates");
    if (!near(edge.ft_rates[0], 0.20) || !near(edge.ft_rates[1], 0.40) ||
        !near(edge.ft_rates[2], 0.80) || !near(edge.ft_rates[3], 1.00)) {
        return fail("PORT column must not be parsed as an FT conversion rate");
    }
    return 0;
}

int testHorizontalChannelCapacityUsesHeight() {
    Problem p;
    p.outline_w = 70.0;
    p.outline_h = 100.0;
    p.blocks = {
        Block{"SRC", 1000.0, 1000.0, 10.0, 100.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"DST", 1000.0, 1000.0, 10.0, 100.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    p.block_index["SRC"] = 0;
    p.block_index["DST"] = 1;
    // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section 1.2): single
    // charge per traversal, not the retired per-edge double-charge (1250, well under cap=25*h=2500,
    // not the old "2*1250=2500=exactly at cap" rationale) -- the assertion below (zero overflow)
    // still holds, now with slack to spare rather than landing exactly at the boundary.
    p.bundles.push_back(Bundle{0, 1, 1250});

    Placement placement;
    placement.rects = {Rect{0.0, 0.0, 10.0, 100.0}, Rect{60.0, 0.0, 10.0, 100.0}};
    placement.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, placement);
    if (r.open_routes != 0) return fail("simple channel route should not be open");
    if (r.channel_overflow > 1e-6) {
        return fail("1250 horizontal nets should fit in a 50x100 channel (V5: single charge, 1250 << cap=25*h=2500)");
    }
    return 0;
}

int testFeedthroughMismatchIsMeasured() {
    Problem p;
    p.outline_w = 200.0;
    p.outline_h = 100.0;
    p.blocks = {
        Block{"SRC", 1000.0, 1000.0, 10.0, 100.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"MID", 10000.0, 10000.0, 0.0, 0.0, 1.0, 1.0, BlockType::Soft, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"DST", 1000.0, 1000.0, 10.0, 100.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    p.block_index["SRC"] = 0;
    p.block_index["MID"] = 1;
    p.block_index["DST"] = 2;
    p.bundles.push_back(Bundle{0, 2, 2000});

    Placement placement;
    placement.rects = {
        Rect{0.0, 0.0, 10.0, 100.0},
        Rect{10.0, 0.0, 100.0, 100.0},
        Rect{110.0, 0.0, 10.0, 100.0},
    };
    placement.aspect = {1.0, 1.0, 1.0};

    RoutingResult r = routeProblem(p, placement);
    if (r.open_routes != 0) return fail("soft feedthrough route should not be open");
    if (r.feedthrough_mismatch <= 1e-6) {
        return fail("unexpanded soft block feedthrough should produce area mismatch");
    }
    return 0;
}

int testLargeBundleSplitsAcrossParallelChannels() {
    Problem p;
    p.outline_w = 200.0;
    p.outline_h = 100.0;
    p.blocks = {
        Block{"SRC", 1000.0, 1000.0, 10.0, 100.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"OBS", 200.0, 200.0, 10.0, 20.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"DST", 1000.0, 1000.0, 10.0, 100.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    p.block_index["SRC"] = 0;
    p.block_index["OBS"] = 1;
    p.block_index["DST"] = 2;
    p.bundles.push_back(Bundle{0, 2, 1800});

    Placement placement;
    placement.rects = {
        Rect{0.0, 0.0, 10.0, 100.0},
        Rect{90.0, 40.0, 10.0, 20.0},
        Rect{190.0, 0.0, 10.0, 100.0},
    };
    placement.aspect = {1.0, 1.0, 1.0};

    RoutingResult r = routeProblem(p, placement);
    if (r.open_routes != 0) return fail("parallel channel route should not be open");
    if (r.routes.size() < 2) return fail("large bundle should be split into multiple PATH records");
    // Per-edge channel demand + entry-axis chunk-sizing (deferred gap) overshoot the 40um parallel
    // channels here, so this fixture no longer asserts zero overflow; it verifies the SPLIT + net-
    // conservation, which is its title's intent.
    int total_nets = 0;
    for (const auto& route : r.routes) total_nets += route.nets;
    if (total_nets != 1800) return fail("split routes must preserve total net demand");
    // Beta Task 3 review Finding 3: Task 3 dropped the overflow assertion entirely when it stopped
    // being exactly zero (see comment above), leaving a future overflow blow-up invisible here. Pin
    // the CURRENT observed value with a tight tolerance, same pattern as regression_p2_20260707.cpp's
    // testAdaptiveChunkingSplitsAtBottleneck (pins 4800 +/- 1e-3): confirmed deterministic (re-run
    // twice, bit-identical at setprecision(17)) rather than derived by hand.
    //
    // V5 RE-BASELINE (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section 1.2;
    // re-measured via a scratch probe against the built router, confirmed deterministic across two
    // independent calls): the pinned 8120.0 was a B4-era both-edges-pricing artifact. Under V5's
    // single-charge-per-axis-per-traversal channelPathSlack (no more halving), the greedy split-at-
    // bottleneck loop now sees each parallel channel's TRUE residual capacity and fills them exactly:
    // CH2 (below OBS, rect w=10,h=40 -> hcap=25*40=1000) takes exactly 1000 nets (saturated, zero
    // overflow), CH3 (above OBS, same geometry) takes the remaining 800 (well under its own 1000
    // cap) -- so total channel_overflow is now exactly 0, not 8120.
    if (std::abs(r.channel_overflow - 0.0) >= 1e-3) {
        return fail("channel_overflow drifted from the pinned 0 um^2 -- re-measure and update the pin, don't widen blindly");
    }
    return 0;
}

}  // namespace

int main() {
    if (int rc = testParsePortAndFtRates()) return rc;
    if (int rc = testHorizontalChannelCapacityUsesHeight()) return rc;
    if (int rc = testFeedthroughMismatchIsMeasured()) return rc;
    if (int rc = testLargeBundleSplitsAcrossParallelChannels()) return rc;
    std::cerr << "regression_20260608: ok\n";
    return 0;
}
