#pragma once

#include <cmath>

#include "model.hpp"

namespace efgr {

// Official FT tiers (20260608 spec): <=3000, <=6000, <=9000, >9000.
inline double ftTierRate(const Block& block, double nets) {
    if (block.ft_rates.empty()) return 0.0;
    if (nets <= 3000.0) return block.ft_rates[0];
    if (nets <= 6000.0) return block.ft_rates.size() > 1 ? block.ft_rates[1] : block.ft_rates.back();
    if (nets <= 9000.0) return block.ft_rates.size() > 2 ? block.ft_rates[2] : block.ft_rates.back();
    return block.ft_rates.size() > 3 ? block.ft_rates[3] : block.ft_rates.back();
}

// Per-side extend, rounded up unconditionally (QA A34 / spec p.8).
inline double ftExtend(double nets, double rate) {
    return std::ceil((nets / 25.0) * rate * 0.5);
}

inline double round2(double v) { return std::round(v * 100.0) / 100.0; }

// New required area from the block's ORIGINAL input area (QA A32), 2-decimal rounded.
inline double ftRequiredArea(double base_area, double extend) {
    double side = std::sqrt(std::max(1.0, base_area)) + extend;
    return round2(side * side);
}

}  // namespace efgr
