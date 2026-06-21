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
    int open_routes = 0;
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
