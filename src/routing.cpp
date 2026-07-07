#include "routing.hpp"

#include <queue>
#include <set>

#include "ft.hpp"
#include "route_cost.hpp"

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

bool portAllows(const Block& block, int edge) {
    if (!block.port_edges.empty()) {
        return std::find(block.port_edges.begin(), block.port_edges.end(), edge) != block.port_edges.end();
    }
    return block.port_edge <= 0 || block.port_edge == edge;
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

RoutingResult routeProblem(const Problem& problem, const Placement& placement, bool cost_driven) {
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
        if (u < n_blocks && (u == source || u == target)) {
            if (!portAllows(problem.blocks[u], edgeFromTo(rects[u], rects[v]))) return false;
        }
        if (v < n_blocks && (v == source || v == target)) {
            if (!portAllows(problem.blocks[v], edgeFromTo(rects[v], rects[u]))) return false;
        }
        return true;
    };

    // P2 Task 3 (design p2-design.md §5.1-5.4; plan Global Constraint 8): `routeChunk` is split into
    // a pathfinding phase (`findRoutePath`, priced Dijkstra only, no accrual/mutation of `result`
    // except the open-route bookkeeping) and a commit phase (`commitRoutePath`, given an already-
    // found `node_path`, computes guides/wirelength/RouteStep and accrues usage/soft_feedthrough_nets
    // at a CALLER-CHOSEN `nets` size). This split exists because the greedy bottleneck loop (§5.2)
    // must find the cheapest path once (priced at a probe size), then commit only a SMALLER chunk of
    // that SAME path once its bottleneck slack is known -- re-running Dijkstra a second time at the
    // smaller size is NOT guaranteed to reproduce the same path (a soft-block FT edge's marginal cost
    // has a tier-boundary kink, ft.hpp's ftTierRate jumps at 3000/6000/9000, so the cross-path cost
    // ranking is not provably stable as the priced `nets` shrinks -- see the design's monotonicity
    // caveat, §3.3). The legacy `routeChunk` wrapper below composes find+commit at the SAME `nets` for
    // both, so cost_driven=false remains byte-identical to pre-T3 behavior (never shrinks after
    // finding), and cost_driven=true's non-adaptive open-route fallthrough behaves identically too.
    auto findRoutePath = [&](const Bundle& bundle, int nets) {
        Route route;
        route.source = bundle.source;
        route.target = bundle.target;
        // NOTE: route.nets is intentionally NOT set here -- it is a probe-priced search, and the
        // caller may commit a smaller chunk of this path than `nets`. commitRoutePath sets route.nets
        // to the actually-committed size.
        // P2 Task 2 (design p2-design.md §Q1-Q2 core; plan Global Constraint 7 comparison gate):
        // cost_driven=false keeps the legacy two-pass feasibility search byte-identical (pass 0
        // channels-only, pass 1 adds soft-block feedthrough as a fallback). cost_driven=true runs a
        // SINGLE unified Dijkstra: soft blocks are always allowed as intermediates (never a
        // fallback-only resource) and every edge is priced in the scorer's own Cost units via
        // route_cost.hpp, so the shortest path is the min-marginal-Delta-Cost path (channel overflow
        // vs FT tier jump compete honestly, GC5 direct-abut stays 0, GC5 never-open is enforced by
        // routeChunk's existing "if node_path empty -> open" fallthrough below, unchanged).
        int pass_count = cost_driven ? 1 : 2;
        for (int pass = 0; pass < pass_count && route.node_path.empty(); ++pass) {
            bool allow_soft = cost_driven ? true : (pass == 1);
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
                    double cost;
                    if (cost_driven) {
                        // GC5: direct block<->block abut (both endpoints, zero channel, zero guide
                        // hops) costs exactly wireCost's manhattan term with dist 0 in practice for a
                        // 2-node path -- but pricing here is per-EDGE during search, so a block-to-
                        // block hop (no channel/soft-FT component) is priced at wireCost only; the
                        // eventual zero-WL abut case (§3.1/A31) falls out because the true guiding-
                        // point WL for a 2-node path is 0 regardless of this proxy (route.wirelength
                        // is recomputed from guides after the path is fixed, routing.cpp below).
                        double dist_uv = manhattan(rects[u].cx(), rects[u].cy(), rects[v].cx(), rects[v].cy());
                        cost = wireCost(problem.alpha, nets, dist_uv);
                        if (v < n_blocks && isSoft(problem.blocks[v].type) && v != bundle.target) {
                            // Soft-block feedthrough intermediate: price the marginal FT tier delta
                            // (design §3.3) using nets already accumulated through this block THIS
                            // routing call so far (call-local state, GC6 purity/determinism -- no
                            // static/global) plus this chunk's n, against the block's placed area.
                            double acc = result.soft_feedthrough_nets[v];
                            double placed_area = round2(placement.rects[v].w * placement.rects[v].h);
                            cost += softFtMarginalCost(problem.blocks[v], acc, nets, placed_area);
                        }
                        if (v >= n_blocks) {
                            // Channel hop: direction-classify from rect adjacency (edgeFromTo), NOT
                            // from guiding points (those are only known after the path is committed,
                            // routing.cpp guides loop below). edgeFromTo(u,v) in {1,3} means u,v share
                            // a vertical edge (side-by-side, x differs) -- a HORIZONTAL traversal,
                            // exactly the dx>kEps case the post-hoc usage accrual (below) tests; {2,4}
                            // means a shared horizontal edge (stacked, y differs) -- VERTICAL
                            // traversal, the dy>kEps case. This is an EXACT match to the accrual's own
                            // classification (not merely a monotone approximation), because both read
                            // the same rect-touch geometry; an L-shaped hop that changes both x and y
                            // cannot occur for a single rect-to-rect touch (positiveTouch requires a
                            // shared edge on exactly one axis), so at most one of the two components
                            // is ever priced per hop at COMMIT time here. Pricing itself, however, uses
                            // only this single entry-edge axis as an admissible one-sided proxy: a path
                            // that enters a channel on one axis (priced here) and exits it on the
                            // orthogonal axis accrues BOTH horizontal_usage and vertical_usage at commit
                            // (the dual `if`s below, routing.cpp accrual loop), while only the entry
                            // axis was charged during the Dijkstra search. This is a deliberate,
                            // deferred gap (design §3.1/R2: the search cost is a monotone proxy, not an
                            // exact accounting of the committed accrual) -- not a claim that no corner
                            // case exists. To be measured at T6/T7 once the full router is wired in.
                            const auto& ch = result.channels[v - n_blocks];
                            int dir = edgeFromTo(rects[u], rects[v]);
                            bool horizontal = (dir == 1 || dir == 3);
                            double usage = horizontal ? ch.horizontal_usage : ch.vertical_usage;
                            cost += channelMarginalCost(ch, horizontal, usage, nets);
                        }
                    } else {
                        cost = manhattan(rects[u].cx(), rects[u].cy(), rects[v].cx(), rects[v].cy());
                        if (v < n_blocks && isSoft(problem.blocks[v].type) && v != bundle.target) cost += nets * 0.05;
                        if (v >= n_blocks) {
                            const auto& ch = result.channels[v - n_blocks];
                            double hcap = std::max(1.0, ch.rect.h * 25.0);
                            double vcap = std::max(1.0, ch.rect.w * 25.0);
                            cost += 10000.0 * nets * (ch.horizontal_usage / hcap + ch.vertical_usage / vcap);
                        }
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
        }
        return route;
    };

    // Commits a chunk of `nets` nets along an ALREADY-FOUND `node_path` (from `findRoutePath`):
    // computes guides/wirelength/RouteStep and accrues channel usage / soft_feedthrough_nets at this
    // `nets` size, which may be smaller than the size `node_path` was originally priced at (§5.2's
    // "route chunk of n along `path`" -- the probe finds the path, the commit routes only `n <= slack`
    // of it). Caller guarantees `node_path` is non-empty (an open route never reaches here).
    auto commitRoutePath = [&](const Bundle& bundle, const std::vector<int>& node_path, int nets) {
        Route route;
        route.source = bundle.source;
        route.target = bundle.target;
        route.nets = nets;
        route.node_path = node_path;

        std::vector<std::pair<double, double>> guides;
        for (std::size_t i = 0; i + 1 < route.node_path.size(); ++i) {
            int a = route.node_path[i], b = route.node_path[i + 1];
            guides.push_back(guidePoint(rects[a], rects[b]));
        }
        for (std::size_t i = 0; i + 1 < guides.size(); ++i) {
            route.wirelength += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second) * nets;
        }

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

    // Legacy-shaped wrapper: find a path priced at `nets` and immediately commit the WHOLE `nets` of
    // it along that same path -- this is what routeChunk always did pre-T3 (never shrinks after
    // finding), so composing find+commit at the SAME size reproduces byte-identical behavior for
    // cost_driven=false and for cost_driven=true's non-adaptive open-route case.
    auto routeChunk = [&](const Bundle& bundle, int nets) {
        Route path = findRoutePath(bundle, nets);
        if (path.node_path.empty()) return path;  // open=true, open_routes already incremented
        return commitRoutePath(bundle, path.node_path, nets);
    };

    // Bottleneck slack along an already-found path, priced at the SAME `nets` that found it (design
    // §5.2 step 2): the minimum, over channel/soft-FT intermediates on the path, of "how many more
    // nets fit before this resource's next boundary" (cap for channels, next FT tier threshold for
    // soft blocks). Returns a value >= 0 (channels/soft blocks already at or past their boundary
    // contribute 0, correctly signaling "no slack left, saturated"). Direction classification reuses
    // the EXACT entry-edge axis the pricing loop above uses (edgeFromTo(rects[u],rects[v]) in {1,3} =
    // horizontal, {2,4} = vertical) -- not the post-hoc guiding-point dx/dy the commit accrual uses,
    // since slack must be computed from the SAME axis the Dijkstra pricing charged (documented gap
    // between entry-axis pricing and dual-axis commit accrual, see the channel-hop comment above).
    auto pathSlack = [&](const std::vector<int>& node_path) {
        double slack = std::numeric_limits<double>::infinity();
        for (std::size_t i = 1; i + 1 < node_path.size(); ++i) {
            int u = node_path[i - 1];
            int node = node_path[i];
            if (node < n_blocks && isSoft(problem.blocks[node].type)) {
                double acc = result.soft_feedthrough_nets[node];
                double node_slack;
                if (acc >= 9000.0) {
                    // Top FT tier has no further boundary -- unbounded slack for splitting purposes
                    // (design §5.2 step 2 parenthetical), so this resource never becomes the
                    // bottleneck; leave it out of the min by using +inf.
                    node_slack = std::numeric_limits<double>::infinity();
                } else {
                    double next_threshold = acc < 3000.0 ? 3000.0 : (acc < 6000.0 ? 6000.0 : 9000.0);
                    node_slack = std::max(0.0, next_threshold - acc);
                }
                slack = std::min(slack, node_slack);
            } else if (node >= n_blocks) {
                const auto& ch = result.channels[node - n_blocks];
                int dir = edgeFromTo(rects[u], rects[node]);
                bool horizontal = (dir == 1 || dir == 3);
                double usage = horizontal ? ch.horizontal_usage : ch.vertical_usage;
                double cap = horizontal ? 25.0 * std::max(1.0, ch.rect.h) : 25.0 * std::max(1.0, ch.rect.w);
                slack = std::min(slack, std::max(0.0, cap - usage));
            }
        }
        return slack;
    };

    for (const auto& bundle : problem.bundles) {
        if (!cost_driven) {
            // Legacy fixed split (byte-identical to pre-T3, the hard global constraint): unchanged.
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
            continue;
        }

        // P2 Task 3 (design p2-design.md §5.2): greedy split-at-bottleneck, cost_driven=true ONLY.
        // Total-nets-conserved (Sigma chunk nets == bundle.nets) AND chunk-count<=kMaxChunks are BOTH
        // hard invariants (task brief), so the LAST permitted iteration always takes the entire
        // remainder regardless of slack/kMinChunk clamping -- without this, a bundle whose bottleneck
        // keeps clamping to kMinChunk (bundle.nets/64) could need up to 64 chunks to fully consume,
        // exceeding kMaxChunks=16 and leaving nets unrouted. Forcing full-remainder on the last slot
        // makes the loop always terminate with remaining==0 within kMaxChunks iterations.
        double min_chunk = kMinChunkFor(bundle.nets);
        int remaining = bundle.nets;
        int chunks_emitted = 0;
        while (remaining > 0 && chunks_emitted < kMaxChunks) {
            Route probe = findRoutePath(bundle, remaining);
            if (probe.node_path.empty()) {
                // No path exists at all -- nets-independent (topology-only reachability, §5.2's
                // "route chunk of n along path" has no path to route). Open the WHOLE remainder as
                // one route, matching the legacy "never re-probe an unreachable bundle" behavior;
                // open_routes was already incremented by findRoutePath.
                probe.source = bundle.source;
                probe.target = bundle.target;
                probe.nets = remaining;
                result.routes.push_back(probe);
                break;
            }
            bool last_slot = (chunks_emitted == kMaxChunks - 1);
            int n;
            if (last_slot) {
                n = remaining;
            } else {
                double slack = pathSlack(probe.node_path);
                if (slack > 0.0) {
                    double clamped = std::max(min_chunk, std::min(slack, static_cast<double>(remaining)));
                    n = std::max(1, std::min(remaining, static_cast<int>(std::llround(clamped))));
                } else {
                    n = std::min(remaining, kChunkCeil);
                }
            }
            Route route = commitRoutePath(bundle, probe.node_path, n);
            result.routes.push_back(route);
            remaining -= n;
            ++chunks_emitted;
        }
    }

    // Area-denominated overflow (design p2-design.md §8.1 / R0; plan Global Constraint 2): both
    // channel_overflow and feedthrough_mismatch must be in um^2 so eval.cpp's shared 1e6 weight
    // (eval.cpp:143-144) compares like units -- previously this term was nets-over-capacity while
    // feedthrough_mismatch was um^2, an apples-to-oranges mismatch that hid the official 2.4x FT
    // win from both the scorer and (later) the P2 router. Axis-crossing span choice per Global
    // Constraint 2: a horizontal overflow's missing capacity is a HEIGHT deficit (hcap = 25*h), and
    // the channel spans that deficit along its WIDTH; a vertical overflow's missing capacity is a
    // WIDTH deficit (vcap = 25*w), spanning the channel's HEIGHT. This mirrors respace.cpp's
    // channelDemands axis-crossing invariant (need_w from vertical_usage, need_h from
    // horizontal_usage) but converts the demand into the AREA the scorer must charge, not just a
    // linear-unit demand.
    for (const auto& ch : result.channels) {
        double hcap = 25.0 * std::max(1.0, ch.rect.h);
        double vcap = 25.0 * std::max(1.0, ch.rect.w);
        if (ch.horizontal_usage > hcap) {
            double missing_height = (ch.horizontal_usage - hcap) / 25.0;
            result.channel_overflow += missing_height * ch.rect.w;
        }
        if (ch.vertical_usage > vcap) {
            double missing_width = (ch.vertical_usage - vcap) / 25.0;
            result.channel_overflow += missing_width * ch.rect.h;
        }
    }
    for (std::size_t i = 0; i < result.soft_feedthrough_nets.size(); ++i) {
        double nets = result.soft_feedthrough_nets[i];
        if (nets <= 0.0 || !isSoft(problem.blocks[i].type)) continue;
        // ft_base_area is 0 only for test-constructed Blocks; the parser always sets it.
        double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
        double required_area = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[i], nets)));
        double actual_area = round2(placement.rects[i].w * placement.rects[i].h);
        if (actual_area + kEps < required_area) result.feedthrough_mismatch += required_area - actual_area;
    }
    return result;
}

}  // namespace efgr
