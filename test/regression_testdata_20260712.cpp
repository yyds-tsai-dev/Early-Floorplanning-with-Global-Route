#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/io.hpp"

using namespace efgr;

int main() {
    // (a) case0: BLK01 fixed dims + MAX outline per 20260712 upload (QA A39)
    Problem p0 = parseProblem("test/2026 ICCAD_case.case0.csv");
    assert(std::abs(p0.outline_w - 2917.0) < 1e-9);
    assert(std::abs(p0.outline_h - 2220.0) < 1e-9);
    const Block& blk01 = p0.blocks.at(p0.block_index.at("BLK01"));
    assert(std::abs(blk01.width - 420.0) < 1e-9);
    assert(std::abs(blk01.height - 843.0) < 1e-9);
    assert(std::abs(blk01.area - 354060.0) < 1e-9);

    // (b) case1: BLK11 PORT EDGE now 3 (QA A36 — blank was erroneous)
    Problem p1 = parseProblem("test/2026 ICCAD_case.case1.csv");
    const Block& blk11 = p1.blocks.at(p1.block_index.at("BLK11"));
    assert(blk11.port_edge == 3 || (!blk11.port_edges.empty() && blk11.port_edges[0] == 3));

    // (c) case3: diagonal matrix entries must NOT become self-bundles (QA A44 errata)
    Problem p3 = parseProblem("test/2026 ICCAD_case.case3.csv");
    for (const Bundle& b : p3.bundles) assert(b.source != b.target);

    std::puts("testdata 20260712 regressions passed");
    return 0;
}
