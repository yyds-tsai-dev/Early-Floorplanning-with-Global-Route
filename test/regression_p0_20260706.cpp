#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/ft.hpp"

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
}  // namespace

int main() {
    testFtMathOfficialExamples();
    std::cout << "p0 regressions passed\n";
    return 0;
}
