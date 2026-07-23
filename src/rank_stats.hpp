#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace efgr {

// B6 (rank-fidelity instrumentation, .superpowers/sdd/b6-brief.md; research doc
// docs/research/2026-07-23-research-refresh-and-strategy.md §2 lit: saACM-ES GECCO 2012 --
// "adapt surrogate trust to measured Kendall tau between cheap and exact rankings, computed from
// pairs already paid for"; Lau & Ho JOTA 1997 -- alignment probability sizes the nomination set
// against proxy error; Gelfand & Mitter JOTA 1989 -- tolerable eval error must shrink with
// temperature). Pure, header-only, dependency-free (STL only, no model.hpp) rank-correlation
// helpers over (cheap, exact) score-pair samples. Consumed by search.cpp's runPlacementSearch
// (SearchResult.fidelity_pairs / elite_pairs) and main.cpp's stderr summary; unit-tested directly
// against hand-computed sequences in test/regression_b6_20260724.cpp.

// Tie tolerance for the rank-correlation helpers below. Deliberately its OWN constant (not
// model.hpp's kEps=1e-6) -- this header has no dependency on model.hpp by design (see brief), and
// the values it compares (raw score.value doubles, or small hand-computed test sequences) do not
// share model.hpp's geometry-tolerance semantics.
constexpr double kTieEpsilon = 1e-9;

// Trailing-window size shared by the reporting "tau_w" stderr field (main.cpp) and the flag-gated
// adaptive exact-interval control (search.cpp) -- one constant so both contexts mean the same
// "window" whenever they say tau_w.
constexpr std::size_t kDefaultFidelityWindow = 64;

// Kendall's tau-b rank correlation coefficient over paired samples (x_i, y_i) =
// pairs[i].first/.second (Kendall 1945). O(n^2) all-pairs comparison -- fine at the sizes this
// module is used at (<=64 SA-loop window entries, <=16 elite-pool entries). Ties handled per the
// standard tau-b denominator: a pair tied on x, y, or both is excluded from the concordant/
// discordant counts, and contributes to n1 (tied-in-x pair count) and/or n2 (tied-in-y pair count)
// respectively -- a pair tied in BOTH contributes to both. Returns 0.0 for n<2 or a degenerate
// (zero-variance on either axis) input, rather than propagating NaN/inf into main.cpp's stderr
// report.
inline double kendallTauB(const std::vector<std::pair<double, double>>& pairs) {
    const std::size_t n = pairs.size();
    if (n < 2) return 0.0;

    long long concordant = 0;
    long long discordant = 0;
    long long tie_x = 0;   // tied in x only (dx==0, dy!=0)
    long long tie_y = 0;   // tied in y only (dy==0, dx!=0)
    long long tie_xy = 0;  // tied in both
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            double dx = pairs[i].first - pairs[j].first;
            double dy = pairs[i].second - pairs[j].second;
            bool tied_x = std::abs(dx) < kTieEpsilon;
            bool tied_y = std::abs(dy) < kTieEpsilon;
            if (tied_x && tied_y) {
                ++tie_xy;
            } else if (tied_x) {
                ++tie_x;
            } else if (tied_y) {
                ++tie_y;
            } else if ((dx > 0.0) == (dy > 0.0)) {
                ++concordant;
            } else {
                ++discordant;
            }
        }
    }

    const double n0 = static_cast<double>(n) * static_cast<double>(n - 1) / 2.0;
    const double n1 = static_cast<double>(tie_x + tie_xy);  // pairs tied in x
    const double n2 = static_cast<double>(tie_y + tie_xy);  // pairs tied in y
    const double denom = std::sqrt((n0 - n1) * (n0 - n2));
    if (denom < kTieEpsilon) return 0.0;
    return (static_cast<double>(concordant) - static_cast<double>(discordant)) / denom;
}

// Kendall tau-b over the trailing `window` entries of `pairs` (or all of them if
// pairs.size() <= window). The SA-loop's "windowed tau" (tau_w): a late-run proxy-rot signal
// distinct from the whole-run overall tau, and the trigger series for search.cpp's flag-gated
// adaptive exact-interval control (--adaptive-interval).
inline double windowedTauB(const std::vector<std::pair<double, double>>& pairs,
                           std::size_t window = kDefaultFidelityWindow) {
    if (pairs.empty()) return 0.0;
    const std::size_t start = pairs.size() > window ? pairs.size() - window : 0;
    std::vector<std::pair<double, double>> tail(pairs.begin() + static_cast<std::ptrdiff_t>(start), pairs.end());
    return kendallTauB(tail);
}

// Spearman's rank correlation coefficient: Pearson correlation of the two variables' ranks, with
// tied values assigned the average rank of their tied group (the standard tie-handling
// convention). Returns 0.0 for n<2 or zero rank-variance on either axis.
inline double spearmanRho(const std::vector<std::pair<double, double>>& pairs) {
    const std::size_t n = pairs.size();
    if (n < 2) return 0.0;

    auto averageRanks = [&](bool use_first) {
        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            double va = use_first ? pairs[a].first : pairs[a].second;
            double vb = use_first ? pairs[b].first : pairs[b].second;
            return va < vb;
        });
        std::vector<double> ranks(n);
        std::size_t i = 0;
        while (i < n) {
            std::size_t j = i;
            double vi = use_first ? pairs[order[i]].first : pairs[order[i]].second;
            while (j + 1 < n) {
                double vj1 = use_first ? pairs[order[j + 1]].first : pairs[order[j + 1]].second;
                if (std::abs(vj1 - vi) >= kTieEpsilon) break;
                ++j;
            }
            // Ranks i..j (0-based positions in `order`) tie; average 1-based rank of the group.
            double avg_rank = (static_cast<double>(i + 1) + static_cast<double>(j + 1)) / 2.0;
            for (std::size_t k = i; k <= j; ++k) ranks[order[k]] = avg_rank;
            i = j + 1;
        }
        return ranks;
    };

    std::vector<double> rx = averageRanks(true);
    std::vector<double> ry = averageRanks(false);

    double mean_x = 0.0, mean_y = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mean_x += rx[i];
        mean_y += ry[i];
    }
    mean_x /= static_cast<double>(n);
    mean_y /= static_cast<double>(n);

    double cov = 0.0, var_x = 0.0, var_y = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double dx = rx[i] - mean_x;
        double dy = ry[i] - mean_y;
        cov += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }
    const double denom = std::sqrt(var_x * var_y);
    if (denom < kTieEpsilon) return 0.0;
    return cov / denom;
}

struct EliteDiagnostics {
    // 1-based rank (by cheap value ascending, i.e. pool order -- index 0 = rank 1) of the pool
    // member whose EXACT value is lowest; 0 if the pool is empty. Answers "how far down the
    // cheap-sorted pool would you have to look to find the placement exact scoring actually
    // prefers" -- the number B5 sizes the elite pool K against (Lau & Ho 1997 alignment
    // probability).
    int exact_best_cheap_rank = 0;
    // True iff the cheap-best pool member (rank 1, index 0) IS the exact-best member -- top-1
    // agreement between the cheap proxy and real routing at the elite pool.
    bool top1_agreement = false;
    // Number of entries in the pool this snapshot was computed over (<= the elite pass's K).
    int pool_size = 0;
};

// `elite_pairs` must be in cheap-rank order (ascending cheap value; index 0 = best cheap
// candidate) -- SearchResult.elite_pairs' documented order (search.hpp).
inline EliteDiagnostics eliteDiagnostics(const std::vector<std::pair<double, double>>& elite_pairs) {
    EliteDiagnostics diag;
    diag.pool_size = static_cast<int>(elite_pairs.size());
    if (elite_pairs.empty()) return diag;
    std::size_t best_idx = 0;
    for (std::size_t i = 1; i < elite_pairs.size(); ++i) {
        if (elite_pairs[i].second < elite_pairs[best_idx].second) best_idx = i;
    }
    diag.exact_best_cheap_rank = static_cast<int>(best_idx) + 1;
    diag.top1_agreement = (best_idx == 0);
    return diag;
}

}  // namespace efgr
