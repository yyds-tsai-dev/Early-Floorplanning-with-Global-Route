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

double ftRateFor(const Block& block, double nets) {
    if (block.ft_rates.empty()) return 0.0;
    if (nets <= 3000.0) return block.ft_rates[0];
    if (nets <= 6000.0) return block.ft_rates.size() > 1 ? block.ft_rates[1] : block.ft_rates.back();
    if (nets <= 9000.0) return block.ft_rates.size() > 2 ? block.ft_rates[2] : block.ft_rates.back();
    return block.ft_rates.size() > 3 ? block.ft_rates[3] : block.ft_rates.back();
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
    result.soft_feedthrough_nets.assign(problem.blocks.size(), 0.0);
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

    auto endpointEdgeAllowed = [&](int u, int v, int source, int target) {
        if (u < n_blocks && (u == source || u == target) && problem.blocks[u].port_edge > 0) {
            if (edgeFromTo(rects[u], rects[v]) != problem.blocks[u].port_edge) return false;
        }
        if (v < n_blocks && (v == source || v == target) && problem.blocks[v].port_edge > 0) {
            if (edgeFromTo(rects[v], rects[u]) != problem.blocks[v].port_edge) return false;
        }
        return true;
    };

    auto routeChunk = [&](const Bundle& bundle, int nets) {
        Route route;
        route.source = bundle.source;
        route.target = bundle.target;
        route.nets = nets;
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
                    if (!endpointEdgeAllowed(u, v, bundle.source, bundle.target)) continue;
                    double cost = manhattan(rects[u].cx(), rects[u].cy(), rects[v].cx(), rects[v].cy());
                    if (v < n_blocks && isSoft(problem.blocks[v].type) && v != bundle.target) cost += nets * 0.05;
                    if (v >= n_blocks) {
                        const auto& ch = result.channels[v - n_blocks];
                        double hcap = std::max(1.0, ch.rect.h * 25.0);
                        double vcap = std::max(1.0, ch.rect.w * 25.0);
                        cost += 10000.0 * nets * (ch.horizontal_usage / hcap + ch.vertical_usage / vcap);
                    }
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
            return route;
        }

        std::vector<std::pair<double, double>> guides;
        for (std::size_t i = 0; i + 1 < route.node_path.size(); ++i) {
            int a = route.node_path[i], b = route.node_path[i + 1];
            guides.push_back(guidePoint(rects[a], rects[b]));
        }
        for (std::size_t i = 0; i + 1 < guides.size(); ++i) {
            route.wirelength += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second) * nets;
        }
        if (guides.size() == 1) route.wirelength += nets;

        for (std::size_t i = 1; i + 1 < route.node_path.size(); ++i) {
            int node = route.node_path[i];
            if (node < n_blocks && isSoft(problem.blocks[node].type)) {
                result.soft_feedthrough_nets[node] += nets;
            }
            if (node >= n_blocks && i - 1 < guides.size() && i < guides.size()) {
                auto& ch = result.channels[node - n_blocks];
                double dx = std::abs(guides[i].first - guides[i - 1].first);
                double dy = std::abs(guides[i].second - guides[i - 1].second);
                if (dx > kEps) ch.horizontal_usage += nets;
                if (dy > kEps) ch.vertical_usage += nets;
                ch.usage = std::max(ch.horizontal_usage, ch.vertical_usage);
            }
        }

        for (std::size_t i = 0; i + 1 < route.node_path.size(); ++i) {
            int a = route.node_path[i];
            int b = route.node_path[i + 1];
            std::string name_a = a < n_blocks ? problem.blocks[a].name : result.channels[a - n_blocks].name;
            std::string name_b = b < n_blocks ? problem.blocks[b].name : result.channels[b - n_blocks].name;
            route.steps.push_back(RouteStep{name_a, edgeFromTo(rects[a], rects[b])});
            route.steps.push_back(RouteStep{name_b, edgeFromTo(rects[b], rects[a])});
        }
        result.total_wirelength += route.wirelength;
        return route;
    };

    for (const auto& bundle : problem.bundles) {
        int split_count = std::max(1, static_cast<int>(std::ceil(bundle.nets / 1000.0)));
        split_count = std::min(split_count, 8);
        int remaining = bundle.nets;
        for (int part = 0; part < split_count; ++part) {
            int parts_left = split_count - part;
            int nets = (remaining + parts_left - 1) / parts_left;
            remaining -= nets;
            Route route = routeChunk(bundle, nets);
            result.routes.push_back(route);
        }
    }

    for (const auto& ch : result.channels) {
        double hcap = 25.0 * std::max(1.0, ch.rect.h);
        double vcap = 25.0 * std::max(1.0, ch.rect.w);
        if (ch.horizontal_usage > hcap) result.channel_overflow += ch.horizontal_usage - hcap;
        if (ch.vertical_usage > vcap) result.channel_overflow += ch.vertical_usage - vcap;
    }
    for (std::size_t i = 0; i < result.soft_feedthrough_nets.size(); ++i) {
        double nets = result.soft_feedthrough_nets[i];
        if (nets <= 0.0 || !isSoft(problem.blocks[i].type)) continue;
        double delta = (nets / 25.0) * ftRateFor(problem.blocks[i], nets) * 0.5;
        double side = std::sqrt(std::max(1.0, problem.blocks[i].area)) + delta;
        double required_area = side * side;
        double actual_area = placement.rects[i].w * placement.rects[i].h;
        if (actual_area + kEps < required_area) result.feedthrough_mismatch += required_area - actual_area;
    }
    return result;
}

}  // namespace efgr
