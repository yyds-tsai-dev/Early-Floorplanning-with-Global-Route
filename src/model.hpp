#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace efgr {

constexpr double kEps = 1e-6;

// NOTE (Beta Task 3 reconciliation): the C1 milestone's official overflow-rate coefficients
// (kBetaLin/kBetaTail/kOverflowRateThreshold/kBetaFt) live in route_cost.hpp, NOT here. The stash
// originally placed them at this efgr scope, but src/cfg_score.cpp (the locked golden re-scorer,
// committed after the stash was cut) independently defines its OWN kBetaLin/kBetaTail in an anonymous
// namespace, and cfg_score.cpp includes model.hpp -> the two collide (ambiguous name). Relocating the
// solver-side coefficients to route_cost.hpp -- which cfg_score.cpp does NOT include, and which
// already hosts the sibling routing cost weights kOverflowAreaWeight/kFtMismatchWeight -- keeps the
// oracle byte-untouched while eval.cpp (which includes route_cost.hpp) still sees them unqualified.

enum class BlockType { Edge, Macro, Soft };

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    double right() const { return x + w; }
    double top() const { return y + h; }
    double cx() const { return x + w * 0.5; }
    double cy() const { return y + h * 0.5; }
};

struct Block {
    std::string name;
    double area = 0.0;
    double ft_base_area = 0.0;  // original input area; FT required-area base (never inflated)
    double width = 0.0;
    double height = 0.0;
    double ar_min = 1.0;
    double ar_max = 1.0;
    BlockType type = BlockType::Soft;
    std::vector<std::string> locations;
    std::vector<double> ft_rates;
    int port_edge = 0;
    std::vector<int> port_edges;
};

struct Bundle {
    int source = -1;
    int target = -1;
    int nets = 0;
};

struct Problem {
    std::vector<Block> blocks;
    std::map<std::string, int> block_index;
    std::vector<Bundle> bundles;
    double outline_w = 0.0;
    double outline_h = 0.0;
    double alpha = 1.0;
};

struct Placement {
    std::vector<Rect> rects;
    std::vector<double> aspect;
    double outline_w = 0.0;
    double outline_h = 0.0;
};

struct Channel {
    std::string name;
    Rect rect;
    double usage = 0.0;
    double horizontal_usage = 0.0;
    double vertical_usage = 0.0;
};

struct RouteStep {
    std::string object;
    int edge = 1;
};

struct Route {
    int source = -1;
    int target = -1;
    int nets = 0;
    std::vector<int> node_path;
    std::vector<RouteStep> steps;
    double wirelength = 0.0;
    bool open = false;
};

struct RoutingResult {
    std::vector<Channel> channels;
    std::vector<Route> routes;
    std::vector<double> soft_feedthrough_nets;
    double total_wirelength = 0.0;
    double channel_overflow = 0.0;
    double feedthrough_mismatch = 0.0;
    // C1 milestone: checker-unit (nets / nets-capacity) overflow accrual, RAW 25*h / 25*w capacity
    // (no max(1.0, ·) clamp -- the official checker capacitates on the true channel dimension, e.g.
    // case0 CH6 capacity_h = 25*1.231 = 30.775). Populated by computeOverflowAndMismatch alongside
    // the pre-existing area-denominated channel_overflow field above (that field is unchanged and
    // still drives respace.cpp/search.cpp comparisons; these two are additive, scorer-only inputs
    // for eval.cpp's global overflow-rate penalty).
    double channel_overflow_nets = 0.0;
    double channel_capacity_total = 0.0;
    int open_routes = 0;
    // P2 Task 5.1 (design p2-design.md §9 T5, controller amendment: comparison gate). Telemetry only
    // -- true iff routeProblem(..., cost_driven=true) returned the legacy two-pass result because it
    // won (or tied) the lexicographic (open_routes, cost) comparison gate. Never read by scoring or
    // caching logic (eval.cpp/search.cpp); exists so T7's sweep can report how often the gate saved us.
    // Always false when cost_driven=false (legacy path never sets it).
    bool used_fallback = false;
};

struct Score {
    double value = std::numeric_limits<double>::infinity();
    double outline_area = 0.0;
    double wirelength = 0.0;
    double penalty = 0.0;
};

inline bool isSoft(BlockType type) { return type == BlockType::Soft; }
inline bool isEdge(BlockType type) { return type == BlockType::Edge; }
inline bool isMacro(BlockType type) { return type == BlockType::Macro; }

inline bool overlaps(const Rect& a, const Rect& b) {
    return a.x + kEps < b.right() && b.x + kEps < a.right() &&
           a.y + kEps < b.top() && b.y + kEps < a.top();
}

inline double overlapArea(const Rect& a, const Rect& b) {
    double ox = std::min(a.right(), b.right()) - std::max(a.x, b.x);
    double oy = std::min(a.top(), b.top()) - std::max(a.y, b.y);
    if (ox <= 0.0 || oy <= 0.0) return 0.0;
    return ox * oy;
}

inline double manhattan(double ax, double ay, double bx, double by) {
    return std::abs(ax - bx) + std::abs(ay - by);
}

inline std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

}  // namespace efgr
