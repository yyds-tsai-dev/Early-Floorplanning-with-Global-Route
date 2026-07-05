#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/ft.hpp"
#include "../src/routing.hpp"

namespace {
using namespace efgr;

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

Block softBlockWithRates() {
    Block b;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};
    return b;
}

void testFtMathOfficialExamples() {
    Block b = softBlockWithRates();
    assert(near(ftTierRate(b, 1000.0), 0.2));
    assert(near(ftTierRate(b, 3500.0), 0.4));
    assert(near(ftTierRate(b, 6711.0), 0.8));
    assert(near(ftTierRate(b, 12013.0), 1.0));

    assert(near(ftExtend(1000.0, 0.2), 4.0));
    assert(near(ftExtend(3500.0, 0.4), 28.0));
    assert(near(ftExtend(6711.0, 0.8), 108.0));   // 107.376 -> ceil
    assert(near(ftExtend(12013.0, 1.0), 241.0));  // 240.26 -> ceil

    assert(near(ftRequiredArea(87500.0, 4.0), 89882.43));
    assert(near(ftRequiredArea(87500.0, 28.0), 104849.02));
    assert(near(ftRequiredArea(87500.0, 108.0), 163057.66));
    assert(near(ftRequiredArea(87500.0, 241.0), 288158.52));
    std::cout << "testFtMathOfficialExamples ok\n";
}

Problem forcedFtProblem() {
    Problem p;
    auto addBlock = [&](const std::string& name, double area, double w, double h, BlockType type) {
        Block b;
        b.name = name;
        b.area = area;
        b.width = w;
        b.height = h;
        b.type = type;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addBlock("BLK01", 30000.0, 100.0, 300.0, BlockType::Macro);
    addBlock("BLK02", 90000.0, 300.0, 300.0, BlockType::Soft);
    addBlock("BLK03", 30000.0, 100.0, 300.0, BlockType::Macro);
    p.outline_w = 500.0;  // exactly the row bbox: no channels anywhere
    p.outline_h = 300.0;
    p.alpha = 1.0;
    Bundle bd;
    bd.source = 0;
    bd.target = 2;
    bd.nets = 900;  // single chunk, tier 1
    p.bundles.push_back(bd);
    return p;
}

Placement rowPlacement() {
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 300.0}, Rect{100.0, 0.0, 300.0, 300.0}, Rect{400.0, 0.0, 100.0, 300.0}};
    pl.aspect = {1.0, 1.0, 1.0};
    return pl;
}

void testRoutingMismatchUsesCeiledMath() {
    Problem p = forcedFtProblem();
    Placement pl = rowPlacement();
    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);
    assert(r.soft_feedthrough_nets[1] > 0.0);  // FT through BLK02 was forced
    double required = ftRequiredArea(90000.0, ftExtend(900.0, 0.2));  // extend = ceil(3.6) = 4 -> (300+4)^2 = 92416.00
    assert(near(required, 92416.00));
    assert(near(r.feedthrough_mismatch, required - 90000.0, 1e-3));
    std::cout << "testRoutingMismatchUsesCeiledMath ok\n";
}
}  // namespace

int main() {
    testFtMathOfficialExamples();
    testRoutingMismatchUsesCeiledMath();
    std::cout << "p0 regressions passed\n";
    return 0;
}
