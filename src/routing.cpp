#include "routing.hpp"

#include <queue>
#include <set>

namespace efgr {
namespace {

bool positiveTouch(const Rect& a, const Rect& b) {
    bool vertical = (std::abs(a.right() - b.x) < kEps || std::abs(b.right() - a.x) < kEps) &&
                    std::min(a.top(), b.top()) - std::max(a.y, b.y) > kEps;
    bool horizontal = (std::abs(a.top() - b.y) < kEps || std::abs(b.top() - a.y) < kEps) &&
                      std::min(a.right(), b.right()) - std::max(a.x, b.x) > kEps;
    return vertical || horizontal;
}

int edgeFromTo(const Rect& a, const Rect& b) {
    if (std::abs(a.x - b.right()) < kEps) return 1;
    if (std::abs(a.top() - b.y) < kEps) return 2;
    if (std::abs(a.right() - b.x) < kEps) return 3;
    return 4;
}

std::pair<double, double> guidePoint(const Rect& a, const Rect& b) {
    if (std::abs(a.right() - b.x) < kEps || std::abs(b.right() - a.x) < kEps) {
        double x = std::abs(a.right() - b.x) < kEps ? a.right() : b.right();
        double lo = std::max(a.y, b.y);
        double hi = std::min(a.top(), b.top());
        return {x, (lo + hi) * 0.5};
    }
    double y = std::abs(a.top() - b.y) < kEps ? a.top() : b.top();
    double lo = std::max(a.x, b.x);
    double hi = std::min(a.right(), b.right());
    return {(lo + hi) * 0.5, y};
}

}  // namespace

std::vector<Channel> extractChannels(const Problem& problem, const Placement& placement) {
    std::vector<double> xs{0.0, problem.outline_w};
    for (const auto& r : placement.rects) {
        xs.push_back(std::max(0.0, std::min(problem.outline_w, r.x)));
        xs.push_back(std::max(0.0, std::min(problem.outline_w, r.right())));
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end(), [](double a, double b) { return std::abs(a - b) < kEps; }), xs.end());

    std::vector<Channel> channels;
    int id = 1;
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        double x0 = xs[i], x1 = xs[i + 1];
        if (x1 - x0 < kEps) continue;
        std::vector<std::pair<double, double>> covered;
        for (const auto& r : placement.rects) {
            if (r.x < x1 - kEps && r.right() > x0 + kEps) {
                covered.push_back({std::max(0.0, r.y), std::min(problem.outline_h, r.top())});
            }
        }
        std::sort(covered.begin(), covered.end());
        double cursor = 0.0;
        for (auto [lo, hi] : covered) {
            if (lo > cursor + kEps) {
                channels.push_back(Channel{"CH" + std::to_string(id++), Rect{x0, cursor, x1 - x0, lo - cursor}, 0.0});
            }
            cursor = std::max(cursor, hi);
        }
        if (cursor < problem.outline_h - kEps) {
            channels.push_back(Channel{"CH" + std::to_string(id++), Rect{x0, cursor, x1 - x0, problem.outline_h - cursor}, 0.0});
        }
    }
    if (channels.empty()) {
        channels.push_back(Channel{"CH1", Rect{0.0, 0.0, problem.outline_w, problem.outline_h}, 0.0});
    }
    return channels;
}

RoutingResult routeProblem(const Problem& problem, const Placement& placement) {
    RoutingResult result;
    result.channels = extractChannels(problem, placement);
    int n_blocks = static_cast<int>(problem.blocks.size());
    int n_channels = static_cast<int>(result.channels.size());
    int total = n_blocks + n_channels;
    std::vector<Rect> rects;
    rects.reserve(total);
    for (const auto& r : placement.rects) rects.push_back(r);
    for (const auto& ch : result.channels) rects.push_back(ch.rect);

    std::vector<std::vector<int>> adj(total);
    for (int i = 0; i < total; ++i) {
        for (int j = i + 1; j < total; ++j) {
            if (positiveTouch(rects[i], rects[j])) {
                adj[i].push_back(j);
                adj[j].push_back(i);
            }
        }
    }

    auto allowed = [&](int node, int source, int target, bool allow_soft) {
        if (node == source || node == target) return true;
        if (node >= n_blocks) return true;
        return allow_soft && isSoft(problem.blocks[node].type);
    };

    for (const auto& bundle : problem.bundles) {
        Route route;
        route.source = bundle.source;
        route.target = bundle.target;
        route.nets = bundle.nets;
        for (int pass = 0; pass < 2 && route.node_path.empty(); ++pass) {
            bool allow_soft = pass == 1;
            std::vector<double> dist(total, std::numeric_limits<double>::infinity());
            std::vector<int> prev(total, -1);
            using Item = std::pair<double, int>;
            std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
            dist[bundle.source] = 0.0;
            pq.push({0.0, bundle.source});
            while (!pq.empty()) {
                auto [d, u] = pq.top();
                pq.pop();
                if (d != dist[u]) continue;
                if (u == bundle.target) break;
                for (int v : adj[u]) {
                    if (!allowed(v, bundle.source, bundle.target, allow_soft)) continue;
                    double cost = manhattan(rects[u].cx(), rects[u].cy(), rects[v].cx(), rects[v].cy());
                    if (v < n_blocks && isSoft(problem.blocks[v].type) && v != bundle.target) cost += bundle.nets * 0.05;
                    if (dist[u] + cost < dist[v]) {
                        dist[v] = dist[u] + cost;
                        prev[v] = u;
                        pq.push({dist[v], v});
                    }
                }
            }
            if (std::isfinite(dist[bundle.target])) {
                for (int at = bundle.target; at != -1; at = prev[at]) route.node_path.push_back(at);
                std::reverse(route.node_path.begin(), route.node_path.end());
            }
        }

        if (route.node_path.empty()) {
            route.open = true;
            result.open_routes++;
            result.routes.push_back(route);
            continue;
        }

        std::vector<std::pair<double, double>> guides;
        for (std::size_t i = 0; i + 1 < route.node_path.size(); ++i) {
            int a = route.node_path[i], b = route.node_path[i + 1];
            guides.push_back(guidePoint(rects[a], rects[b]));
            if (a >= n_blocks) result.channels[a - n_blocks].usage += bundle.nets;
            if (b >= n_blocks) result.channels[b - n_blocks].usage += bundle.nets;
        }
        for (std::size_t i = 0; i + 1 < guides.size(); ++i) {
            route.wirelength += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second) * bundle.nets;
        }
        if (guides.size() == 1) route.wirelength += bundle.nets;

        for (std::size_t i = 0; i + 1 < route.node_path.size(); ++i) {
            int a = route.node_path[i];
            int b = route.node_path[i + 1];
            std::string name_a = a < n_blocks ? problem.blocks[a].name : result.channels[a - n_blocks].name;
            std::string name_b = b < n_blocks ? problem.blocks[b].name : result.channels[b - n_blocks].name;
            route.steps.push_back(RouteStep{name_a, edgeFromTo(rects[a], rects[b])});
            route.steps.push_back(RouteStep{name_b, edgeFromTo(rects[b], rects[a])});
        }
        result.total_wirelength += route.wirelength;
        result.routes.push_back(route);
    }

    for (const auto& ch : result.channels) {
        double cap = 25.0 * std::max(1.0, std::min(ch.rect.w, ch.rect.h));
        if (ch.usage > cap) result.channel_overflow += ch.usage - cap;
    }
    return result;
}

}  // namespace efgr
