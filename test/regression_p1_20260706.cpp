#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "../src/eval.hpp"
#include "../src/ft.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/routing.hpp"
#include "../src/search.hpp"
#include "../src/validate.hpp"

namespace {
using namespace efgr;

Placement twoRectPlacement() {
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 200.0}, Rect{100.0, 0.0, 50.0, 200.0}};
    pl.aspect = {1.0, 1.0};
    return pl;
}

void testPlacementKeyDiffersWithOutline() {
    Placement unset = twoRectPlacement();
    Placement withOutline = twoRectPlacement();
    withOutline.outline_w = 100.0;
    withOutline.outline_h = 200.0;

    std::string key_unset = placementKey(unset);
    std::string key_with_outline = placementKey(withOutline);
    assert(key_unset != key_with_outline);
    std::cout << "testPlacementKeyDiffersWithOutline ok\n";
}

void testPlacementKeyUnsetMatchesTodaysFormat() {
    Placement pl = twoRectPlacement();
    assert(pl.outline_w == 0.0);
    assert(pl.outline_h == 0.0);

    std::ostringstream expected;
    for (const auto& r : pl.rects) {
        expected << std::llround(r.x * 1000.0) << ',' << std::llround(r.y * 1000.0) << ','
                 << std::llround(r.w * 1000.0) << ',' << std::llround(r.h * 1000.0) << ';';
    }
    expected << ':' << std::llround(pl.outline_w * 1000.0) << 'x' << std::llround(pl.outline_h * 1000.0);

    assert(placementKey(pl) == expected.str());
    std::cout << "testPlacementKeyUnsetMatchesTodaysFormat ok\n";
}

}  // namespace

int main() {
    testPlacementKeyDiffersWithOutline();
    testPlacementKeyUnsetMatchesTodaysFormat();
    std::cout << "p1 regressions passed\n";
    return 0;
}
