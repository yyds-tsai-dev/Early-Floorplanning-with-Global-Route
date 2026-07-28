#include "routing.hpp"

#include <array>
#include <queue>
#include <set>
#ifdef EFGR_ROUTE_TRACE
#include <cstdio>
#endif

#include "ft.hpp"
#include "route_cost.hpp"
#include "route_geometry.hpp"

namespace efgr {
namespace {

bool positiveTouch(const Rect& a, const Rect& b) {
    bool vertical = (std::abs(a.right() - b.x) < kEps || std::abs(b.right() - a.x) < kEps) &&
                    std::min(a.top(), b.top()) - std::max(a.y, b.y) > kEps;
    bool horizontal = (std::abs(a.top() - b.y) < kEps || std::abs(b.top() - a.y) < kEps) &&
                      std::min(a.right(), b.right()) - std::max(a.x, b.x) > kEps;
    return vertical || horizontal;
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

    // P2 Task 4 (design p2-design.md §4.2; plan Global Constraint 9): call-local negotiation history,
    // per-channel per-direction (index by channel id, [0]=horizontal, [1]=vertical) and per-soft-block
    // (index by block id, matching soft_feedthrough_nets' own indexing). std::vector indexed by node
    // id, NOT std::map/unordered_map, so history lookup/iteration order is deterministic (GC6 purity/
    // determinism -- no iteration-order dependence). Declared here (call-local to routeProblem, never
    // static/global) so it is zero-initialized once per call and only ever read/written by the
    // cost_driven=true negotiation loop below; cost_driven=false never touches it.
    std::vector<std::array<double, 2>> history_channel(n_channels, std::array<double, 2>{0.0, 0.0});
    std::vector<double> history_ft(n_blocks, 0.0);

    // B4 (design b4-design.md §3.1/§7.2): official per-overflow-net price. `outline_area` is the
    // scorer's own A (eval.cpp's routeAndScoreAt scopes problem.outline_w/h to exactly the outline
    // this call routes at, before calling routeProblem -- so this IS that same declared outline).
    // `raw_capacity_total` is C = Sum_ch(25*h + 25*w), RAW (no max(1.0,.) clamp), over the channels
    // extracted once above -- both fixed for the whole pass, matching computeOverflowAndMismatch's
    // own channel_capacity_total accumulation exactly (same channel list, same RAW formula), just
    // computed up front since neither depends on usage. The initial `p_ovf` is P_ovf = kBetaLin*A/C,
    // the exact derivative of the scorer's linear overflow-rate term d(20*A*R)/d(overflow_nets) --
    // a single scorer-derived rate, not a per-channel area proxy.
    //
    // V5 tail-aware re-pricing (Codex V5-port review finding #2; analysis doc section 6): no longer
    // const -- the official penalty is piecewise (sqrt tail past kOverflowRateThreshold), so the
    // negotiation loop below re-prices p_ovf at the START of every round >= 1 from the PREVIOUS
    // round's global overflow level via roundOverflowPrice (route_cost.hpp: linear derivative /
    // theta-entry secant / past-theta average -- see its doc comment for why a full-rip-up round
    // must charge the AVERAGE, not the endpoint marginal, on the concave tail). Round 0 always
    // keeps this pristine linear expression (there is no previous round), and roundOverflowPrice
    // returns the bit-identical expression while the previous round's (O+1)/C stayed <= threshold
    // -- so any call whose every round sits in the linear regime (all current benchmark cases,
    // R <= 0.00906) prices byte-identically to the pre-refinement router. Only the cost_driven
    // negotiation rounds read p_ovf; the legacy pricing branch and the
    // route_pricing_official=false portfolio leg never touch it.
    const double outline_area = problem.outline_w * problem.outline_h;
    // F-FT (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md §6, Codex finding #1):
    // base FT-converted area (zero crossing nets) summed over EVERY block -- the same per-block value
    // computeOverflowAndMismatch's own base-area logic (below) degenerates to at nets==0 (soft: base
    // via ft_base_area/area fallback; everything else: area). Computed once per routeProblem call
    // (fixed for the whole call, like outline_area/p_ovf below) so `result` and each round's
    // `round_result` can seed rr.ft_area_sum to this floor at construction; commitRoutePath (below)
    // then accrues the exact per-hop ftRequiredDelta on top as FT hops commit, giving the
    // ft_pricing_aggressive Dijkstra pricing (below) a RUNNING rr.ft_area_sum to read mid-route that
    // matches the official V5 aggregate (route_cost.hpp's roundCostOfficial) without waiting for
    // computeOverflowAndMismatch's end-of-call recomputation (which reproduces the same total exactly
    // -- see its own comment below).
    const double base_ft_area_sum = [&] {
        double sum = 0.0;
        for (const auto& block : problem.blocks) {
            sum += isSoft(block.type) ? (block.ft_base_area > 0.0 ? block.ft_base_area : block.area) : block.area;
        }
        return sum;
    }();
    result.ft_area_sum = base_ft_area_sum;
    double raw_capacity_total = 0.0;
    for (const auto& ch : result.channels) raw_capacity_total += 25.0 * ch.rect.h + 25.0 * ch.rect.w;
    double p_ovf = (raw_capacity_total > 0.0) ? kBetaLin * outline_area / raw_capacity_total : 0.0;

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
    //
    // P2 Task 4: `findRoutePath`/`commitRoutePath`/`pathSlack`/`routeChunk` now take an explicit
    // `RoutingResult& rr` (the resource ledger to read/mutate usage on) instead of closing over the
    // single outer `result`, so the negotiation loop (below) can route each round into a fresh,
    // round-local `RoutingResult` while `cost_driven=false` keeps passing the outer `result` directly
    // -- zero behavior change for that path. cost_driven=true's non-negotiated call sites are gone
    // (the round loop is now the ONLY cost_driven=true caller), so the history surcharge below is
    // read unconditionally when cost_driven; round 0 has all-zero history vectors, making the added
    // terms exactly 0 and reproducing plain T3 behavior (task requirement: round 0 == T3-exact).
    // `force_legacy` (P2 Task 5.1 fix, controller amendment): findRoutePath dispatches on the OUTER
    // `cost_driven` parameter captured by reference, which is fixed for the entire routeProblem call --
    // but `runLegacyPass` (below) must run TRUE legacy pricing even when called from WITHIN a
    // cost_driven=true invocation (the new unconditional comparison-gate run). Without this override,
    // `runLegacyPass` inside a cost_driven=true call silently reran cost-driven's OWN single-pass
    // pricing (same `cost_driven` capture, same edge-cost formula) instead of the real two-pass
    // channels-then-soft-fallback legacy algorithm -- a pre-existing bug in T5's original fallback that
    // this parameter fixes: `use_legacy` below is the actual per-call mode, `force_legacy=true`
    // unconditionally selects legacy pricing regardless of the outer flag; the default (false) preserves
    // every existing call site's behavior exactly (`cost_driven=false` top-level calls already had
    // `cost_driven` false, so `use_legacy` degenerates to the same thing either way; the negotiation
    // loop's own calls never pass force_legacy, so they keep reading the outer `cost_driven`, which is
    // always true there).
    //
    // WARNING (P2 Task 6, Rider A of the T5.1 review): any FUTURE call site that wants true legacy
    // (channels-first, then soft-FT fallback) pricing from WITHIN a cost_driven=true routeProblem
    // call MUST pass force_legacy=true explicitly. Omitting it silently reruns cost-driven's own
    // pricing instead (the exact bug class this parameter was added to fix) -- the default `false`
    // only preserves behavior for callers where the outer `cost_driven` is already false.
    auto findRoutePath = [&](RoutingResult& rr, const Bundle& bundle, int nets, bool force_legacy = false) {
        bool use_legacy = force_legacy || !cost_driven;
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
        int pass_count = use_legacy ? 2 : 1;
        for (int pass = 0; pass < pass_count && route.node_path.empty(); ++pass) {
            bool allow_soft = use_legacy ? (pass == 1) : true;
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
                    if (!use_legacy) {
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
                            // (design §3.3/§5) using nets already accumulated through this block THIS
                            // routing call so far (call-local state, GC6 purity/determinism -- no
                            // static/global) plus this chunk's n, against the block's placed area. B4
                            // (design §5): weight the exact mismatch delta by the OFFICIAL kBetaFt=3.0
                            // (the same weight eval.cpp's exact scorer charges feedthrough_mismatch
                            // at) instead of the legacy 1e6 area-denominated proxy -- `3.0*Delta
                            // mismatch` is a provable upper bound on the true post-finalize cost
                            // (design §5: it equals the actual charge whenever paydown fails, the
                            // common tight-packing case).
                            double acc = rr.soft_feedthrough_nets[v];
                            double placed_area = round2(placement.rects[v].w * placement.rects[v].h);
                            // B4.1 (route-pricing portfolio, b4-report.md): `route_pricing_official`
                            // selects official (B4) vs the RETAINED pre-B4 (HEAD 207d08c) FT pricing.
                            // The false branch is byte-identical to that history -- it exists purely
                            // so search.cpp's runPricingPortfolio can run the SAME problem under both
                            // and fold the results, restoring the per-case Pareto floor a changed
                            // search trajectory can otherwise miss.
                            if (problem.route_pricing_official) {
                                if (problem.ft_pricing_aggressive) {
                                    // F-FT (docs/research/2026-07-28-alpha-v5-cost-recalculation-
                                    // analysis.md §6, Codex finding #1): price this FT hop at the
                                    // EXACT marginal of the official V5 AGGREGATE FT penalty
                                    // (route_cost.hpp's roundCostOfficial) -- kBetaFt*max(0,
                                    // ft_area_sum-OutlineArea) -- instead of the per-block
                                    // mismatch-vs-placed-area delta in the else branch below. Free
                                    // (0 marginal cost) while rr.ft_area_sum + dreq stays under
                                    // outline_area, i.e. while the aggregate has slack; the tie-break
                                    // epsilon (kFtAggressiveTieBreak) keeps an officially-free hop from
                                    // beating an equal-WL channel route on an exact tie (see its own
                                    // doc comment, route_cost.hpp). The Q4' outline_after_ft ambiguity
                                    // (qa-draft) is why this is opt-in (defaults false) rather than
                                    // replacing the conservative branch outright -- the conservative
                                    // per-block pricing is retained as the floor leg.
                                    double dreq = ftRequiredDelta(problem.blocks[v], acc, nets);
                                    cost += kBetaFt * (std::max(0.0, rr.ft_area_sum + dreq - outline_area) -
                                                       std::max(0.0, rr.ft_area_sum - outline_area));
                                    cost += kFtAggressiveTieBreak * nets;
                                    cost += kBetaFt * history_ft[v] * nets;
                                } else {
                                    // P2 Task 4 (design §4.2): congested-history surcharge, added on top of
                                    // the marginal FT cost above. Zero at round 0 (history_ft is all-zero
                                    // until the FIRST post-round bump), so this is a strict no-op until a
                                    // later round's history has accumulated. B4 (design §7.3): re-scaled
                                    // from kFtMismatchWeight (1e6) to kBetaFt so the surcharge stays the
                                    // same order as the re-denominated base cost (was ~5000x too large
                                    // otherwise).
                                    cost += kBetaFt * ftMismatchDelta(problem.blocks[v], acc, nets, placed_area);
                                    cost += kBetaFt * history_ft[v] * nets;
                                }
                            } else {
                                cost += softFtMarginalCost(problem.blocks[v], acc, nets, placed_area);
                                cost += kFtMismatchWeight * history_ft[v] * nets;
                            }
                        }
                        if (u >= n_blocks || v >= n_blocks) {
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
                            // shared edge on exactly one axis), so exactly one axis is charged per side
                            // below.
                            //
                            // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md,
                            // section 1.2): a channel traversal is charged once per TOUCHED axis, not
                            // once per edge -- so pricing `u`'s exit hop must be SUPPRESSED when `u`
                            // is itself a channel AND this hop's axis is the SAME axis `u` was already
                            // ENTERED on (that axis was already charged when `prev[u] -> u` was priced,
                            // one level up the search). `prev[u]` is always settled before `u` is
                            // relaxed (standard Dijkstra invariant) and can only be unset (-1) when `u`
                            // is the search source -- always a block, never a channel -- so the
                            // `prev[u] >= 0` check below is a defensive guard, not a reachable branch;
                            // it deliberately does NOT suppress (falls back to always pricing `u`) if
                            // it somehow were unmet. The `v`-side (entry) clause stays unconditional --
                            // `v`'s entry axis is always a NEW charge from this node's perspective,
                            // exactly matching `accumulatePathUsage` (route_geometry.hpp)'s per-axis-
                            // per-traversal accrual at commit time, so the search sees exactly the
                            // demand its own commit will accrue.
                            int dir = edgeFromTo(rects[u], rects[v]);
                            bool horizontal = (dir == 1 || dir == 3);
                            bool u_charge_suppressed = false;
                            if (u >= n_blocks && prev[u] >= 0) {
                                int entry_dir = edgeFromTo(rects[u], rects[prev[u]]);
                                bool entry_horizontal_of_u = (entry_dir == 1 || entry_dir == 3);
                                u_charge_suppressed = (horizontal == entry_horizontal_of_u);
                            }
                            // B4.1 (route-pricing portfolio, b4-report.md): `route_pricing_official`
                            // selects official (V5, single-charge-per-axis) vs the RETAINED pre-B4
                            // (HEAD 207d08c) channel pricing (entry-axis-only -- no `u`-side clause
                            // existed at all). The false branch is byte-identical to that history.
                            if (problem.route_pricing_official) {
                                if (u >= n_blocks && !u_charge_suppressed) {
                                    const auto& ch_u = rr.channels[u - n_blocks];
                                    double usage = horizontal ? ch_u.horizontal_usage : ch_u.vertical_usage;
                                    cost += p_ovf * marginalOverflowNets(ch_u, horizontal, usage, nets);
                                    // P2 Task 4 (design §4.2/§4.3): congested-history surcharge, keyed
                                    // per channel per direction on the SAME axis classification the
                                    // marginal-cost pricing above just used. Zero at round 0. Index [0]
                                    // = horizontal, [1] = vertical (matches the array's doc comment
                                    // above). B4 (design §7.3): re-scaled from kOverflowAreaWeight to
                                    // p_ovf so the surcharge stays the same order as the re-denominated
                                    // base cost (was ~5000x too large otherwise).
                                    double hist = horizontal ? history_channel[u - n_blocks][0] : history_channel[u - n_blocks][1];
                                    cost += p_ovf * hist * nets;
                                }
                                if (v >= n_blocks) {
                                    const auto& ch_v = rr.channels[v - n_blocks];
                                    double usage = horizontal ? ch_v.horizontal_usage : ch_v.vertical_usage;
                                    cost += p_ovf * marginalOverflowNets(ch_v, horizontal, usage, nets);
                                    double hist = horizontal ? history_channel[v - n_blocks][0] : history_channel[v - n_blocks][1];
                                    cost += p_ovf * hist * nets;
                                }
                            } else if (v >= n_blocks) {
                                const auto& ch = rr.channels[v - n_blocks];
                                double usage = horizontal ? ch.horizontal_usage : ch.vertical_usage;
                                cost += channelMarginalCost(ch, horizontal, usage, nets);
                                double hist = horizontal ? history_channel[v - n_blocks][0] : history_channel[v - n_blocks][1];
                                cost += kOverflowAreaWeight * hist * nets;
                            }
                        }
                    } else {
                        cost = manhattan(rects[u].cx(), rects[u].cy(), rects[v].cx(), rects[v].cy());
                        if (v < n_blocks && isSoft(problem.blocks[v].type) && v != bundle.target) cost += nets * 0.05;
                        if (v >= n_blocks) {
                            const auto& ch = rr.channels[v - n_blocks];
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
            rr.open_routes++;
        }
        return route;
    };

    // Commits a chunk of `nets` nets along an ALREADY-FOUND `node_path` (from `findRoutePath`):
    // computes guides/wirelength/RouteStep and accrues channel usage / soft_feedthrough_nets at this
    // `nets` size, which may be smaller than the size `node_path` was originally priced at (§5.2's
    // "route chunk of n along `path`" -- the probe finds the path, the commit routes only `n <= slack`
    // of it). Caller guarantees `node_path` is non-empty (an open route never reaches here).
    auto commitRoutePath = [&](RoutingResult& rr, const Bundle& bundle, const std::vector<int>& node_path, int nets) {
        Route route;
        route.source = bundle.source;
        route.target = bundle.target;
        route.nets = nets;
        route.node_path = node_path;

        // Beta Task 1 (byte-identical refactor, .superpowers/sdd/beta-task-1-brief.md): WL + per-
        // channel usage accumulation now lives in route_geometry.hpp's accumulatePathUsage (verbatim
        // transplant of the former inline loops), so a future .cfg re-scorer can reuse it.
        // node_channels[i] is non-null exactly when route.node_path[i] >= n_blocks, matching the
        // original loop's `node >= n_blocks` test.
        std::vector<Rect> node_rects;
        std::vector<Channel*> node_channels;
        node_rects.reserve(route.node_path.size());
        node_channels.reserve(route.node_path.size());
        for (int node : route.node_path) {
            node_rects.push_back(rects[node]);
            node_channels.push_back(node >= n_blocks ? &rr.channels[node - n_blocks] : nullptr);
        }
        route.wirelength = accumulatePathUsage(node_rects, node_channels, nets);

        for (std::size_t i = 1; i + 1 < route.node_path.size(); ++i) {
            int node = route.node_path[i];
            if (node < n_blocks && isSoft(problem.blocks[node].type)) {
                // F-FT: capture the accumulated-before-this-hop crossing-net count BEFORE mutating it,
                // so ftRequiredDelta prices exactly this hop's marginal required-area contribution --
                // the RUNNING twin of the aggregate computeOverflowAndMismatch recomputes at the end
                // (V5 section 1.4; route_cost.hpp's ftRequiredDelta doc comment).
                double acc = rr.soft_feedthrough_nets[node];
                rr.ft_area_sum += ftRequiredDelta(problem.blocks[node], acc, nets);
                rr.soft_feedthrough_nets[node] += nets;
            }
        }

        for (std::size_t i = 0; i + 1 < route.node_path.size(); ++i) {
            int a = route.node_path[i];
            int b = route.node_path[i + 1];
            std::string name_a = a < n_blocks ? problem.blocks[a].name : rr.channels[a - n_blocks].name;
            std::string name_b = b < n_blocks ? problem.blocks[b].name : rr.channels[b - n_blocks].name;
            route.steps.push_back(RouteStep{name_a, edgeFromTo(rects[a], rects[b])});
            route.steps.push_back(RouteStep{name_b, edgeFromTo(rects[b], rects[a])});
        }
        rr.total_wirelength += route.wirelength;
        return route;
    };

    // Legacy-shaped wrapper: find a path priced at `nets` and immediately commit the WHOLE `nets` of
    // it along that same path -- this is what routeChunk always did pre-T3 (never shrinks after
    // finding), so composing find+commit at the SAME size reproduces byte-identical behavior. `force_legacy`
    // (P2 Task 5.1 fix) forwards to findRoutePath so `runLegacyPass` (below) gets true legacy pricing
    // even when called from within a cost_driven=true routeProblem invocation -- see findRoutePath's
    // `force_legacy` comment. Default false preserves the top-level cost_driven=false caller unchanged.
    auto routeChunk = [&](RoutingResult& rr, const Bundle& bundle, int nets, bool force_legacy = false) {
        Route path = findRoutePath(rr, bundle, nets, force_legacy);
        if (path.node_path.empty()) return path;  // open=true, open_routes already incremented
        return commitRoutePath(rr, bundle, path.node_path, nets);
    };

    // Bottleneck slack along an already-found path, priced at the SAME `nets` that found it (design
    // §5.2 step 2): the minimum, over channel/soft-FT intermediates on the path, of "how many more
    // nets fit before this resource's next boundary" (cap for channels, next FT tier threshold for
    // soft blocks). Returns a value >= 0 (channels/soft blocks already at or past their boundary
    // contribute 0, correctly signaling "no slack left, saturated").
    //
    // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section 1.2): the
    // official branch's channel slack (channelPathSlack, route_cost.hpp) now counts each TOUCHED axis
    // ONCE, regardless of whether one or both of this node's incident hops (entry from predecessor
    // `u = node_path[i-1]`, exit to successor `s = node_path[i+1]`) land on it -- so its residual
    // capacity (`cap - usage`) is used AS-IS, no longer halved for a straight pass-through (both hops
    // on the same axis). This matches route_geometry.hpp's accumulatePathUsage, which charges a
    // touched axis once per traversal, not once per edge -- committing `n <= slack` then accrues
    // exactly `n <= cap` at commit, never overshooting. An L-turn (entry/exit on different axes) still
    // charges one per axis and the TIGHTER axis still binds, unchanged from B4. The legacy branch
    // (`problem.route_pricing_official == false`, below) is UNCHANGED by V5 -- it stays the
    // entry-axis-only slack, byte-identical to its pre-B4 (HEAD 207d08c) history.
    auto pathSlack = [&](RoutingResult& rr, const std::vector<int>& node_path) {
        double slack = std::numeric_limits<double>::infinity();
        for (std::size_t i = 1; i + 1 < node_path.size(); ++i) {
            int u = node_path[i - 1];
            int node = node_path[i];
            if (node < n_blocks && isSoft(problem.blocks[node].type)) {
                double acc = rr.soft_feedthrough_nets[node];
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
                const auto& ch = rr.channels[node - n_blocks];
                double node_slack;
                // B4.1 (route-pricing portfolio, b4-report.md): official (both-edges/halving) vs the
                // RETAINED pre-B4 (HEAD 207d08c) entry-axis-only slack -- the false branch is
                // byte-identical to that history.
                if (problem.route_pricing_official) {
                    int s = node_path[i + 1];
                    node_slack = channelPathSlack(rects[u], ch, rects[s]);
                } else {
                    int dir = edgeFromTo(rects[u], rects[node]);
                    bool horizontal = (dir == 1 || dir == 3);
                    double usage = horizontal ? ch.horizontal_usage : ch.vertical_usage;
                    double cap = horizontal ? 25.0 * std::max(1.0, ch.rect.h) : 25.0 * std::max(1.0, ch.rect.w);
                    node_slack = std::max(0.0, cap - usage);
                }
                slack = std::min(slack, node_slack);
            }
        }
        return slack;
    };

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
    //
    // P2 Task 4: extracted into a lambda (was inline at the end of routeProblem) so BOTH the legacy
    // path and each negotiation round can compute a RoutingResult's overflow/mismatch fields from its
    // own usage snapshot. Pure function of `rr`'s current channel/soft_feedthrough_nets state.
    auto computeOverflowAndMismatch = [&](RoutingResult& rr) {
        for (const auto& ch : rr.channels) {
            double hcap = 25.0 * std::max(1.0, ch.rect.h);
            double vcap = 25.0 * std::max(1.0, ch.rect.w);
            if (ch.horizontal_usage > hcap) {
                double missing_height = (ch.horizontal_usage - hcap) / 25.0;
                rr.channel_overflow += missing_height * ch.rect.w;
            }
            if (ch.vertical_usage > vcap) {
                double missing_width = (ch.vertical_usage - vcap) / 25.0;
                rr.channel_overflow += missing_width * ch.rect.h;
            }
            // C1 milestone: checker-unit overflow/capacity, RAW 25*h / 25*w (no max(1.0, ·) clamp --
            // matches the official checker's capacity_h/capacity_v_by_channel exactly, e.g. case0
            // CH6 capacity_h = 25*1.231 = 30.775). Feeds eval.cpp's global R = overflow/capacity
            // penalty; does not replace the area-denominated channel_overflow accrual above.
            double cap_h = 25.0 * ch.rect.h;
            double cap_v = 25.0 * ch.rect.w;
            rr.channel_capacity_total += cap_h + cap_v;
            if (ch.horizontal_usage > cap_h) rr.channel_overflow_nets += ch.horizontal_usage - cap_h;
            if (ch.vertical_usage > cap_v) rr.channel_overflow_nets += ch.vertical_usage - cap_v;
        }
        for (std::size_t i = 0; i < rr.soft_feedthrough_nets.size(); ++i) {
            double nets = rr.soft_feedthrough_nets[i];
            if (nets <= 0.0 || !isSoft(problem.blocks[i].type)) continue;
            // ft_base_area is 0 only for test-constructed Blocks; the parser always sets it.
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required_area = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[i], nets)));
            double actual_area = round2(placement.rects[i].w * placement.rects[i].h);
            if (actual_area + kEps < required_area) rr.feedthrough_mismatch += required_area - actual_area;
        }

        // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section 1.4):
        // aggregate FT-converted area across EVERY block (not just soft blocks with a nonzero
        // mismatch above) -- feeds the official FTPenalty = kBetaFt*max(0, ft_area_sum-OutlineArea)
        // (route_cost.hpp's roundCostOfficial, eval.cpp's exact-tier scorer). Soft blocks use the
        // SAME base-area logic as the mismatch loop above; nets==0 makes ftExtend/ftRequiredArea
        // degenerate to exactly `base`, so an untouched soft block still contributes its own area,
        // same as any non-soft block.
        // F-FT (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md §6, Codex finding
        // #1): rr.ft_area_sum no longer starts this function at 0.0 -- routeProblem now seeds it to
        // base_ft_area_sum at construction and commitRoutePath accrues ftRequiredDelta incrementally
        // per FT hop (the RUNNING aggregate the ft_pricing_aggressive Dijkstra pricing above reads
        // mid-route). This loop is a FULL independent recomputation from rr.soft_feedthrough_nets'
        // final state, so it must OVERWRITE (reset to 0.0 first) rather than accumulate on top of that
        // running value -- otherwise it would double-count. The reset makes this function idempotent
        // (safe regardless of call count, though every call site still calls it exactly once) and its
        // result is provably the SAME value the running accrual already reached: ftRequiredDelta
        // telescopes (required(0) + sum-of-per-hop-deltas == required(final acc)) to exactly what this
        // loop recomputes from scratch.
        rr.ft_area_sum = 0.0;
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            const Block& block = problem.blocks[i];
            if (isSoft(block.type)) {
                double base = block.ft_base_area > 0.0 ? block.ft_base_area : block.area;
                double nets_i = rr.soft_feedthrough_nets[i];
                rr.ft_area_sum += ftRequiredArea(base, ftExtend(nets_i, ftTierRate(block, nets_i)));
            } else {
                rr.ft_area_sum += block.area;
            }
        }
    };

    // Legacy two-pass feasibility routing (channels-first pass 0, soft-block-feedthrough-fallback
    // pass 1 via `routeChunk`/`findRoutePath` above): fixed ceil(nets/1000) chunking, routed straight
    // into `rr`. Byte-identical in shape to pre-T4. Shared by the cost_driven=false path (below,
    // unchanged) and, as of P2 Task 5 (design p2-design.md §9 T5; plan Global Constraint 10
    // feasibility fallback) / P2 Task 5.1 (comparison gate, controller amendment), the cost_driven=true
    // comparison-gate call further down. `routeChunk(rr, bundle, nets, /*force_legacy=*/true)` (P2
    // Task 5.1 fix) is REQUIRED here, not optional: without it, this loop would silently use the OUTER
    // `cost_driven` flag's pricing (true, when called from the gate) instead of true legacy pricing,
    // defeating the entire comparison -- see findRoutePath's `force_legacy` comment for the full story.
    auto runLegacyPass = [&](RoutingResult& rr) {
        for (const auto& bundle : problem.bundles) {
            int split_count = std::max(1, static_cast<int>(std::ceil(bundle.nets / 1000.0)));
            split_count = std::min(split_count, 8);
            int remaining = bundle.nets;
            for (int part = 0; part < split_count; ++part) {
                int parts_left = split_count - part;
                int nets = (remaining + parts_left - 1) / parts_left;
                remaining -= nets;
                Route route = routeChunk(rr, bundle, nets, /*force_legacy=*/true);
                rr.routes.push_back(route);
            }
        }
        computeOverflowAndMismatch(rr);
    };

    if (!cost_driven) {
        // Legacy path (byte-identical to pre-T4, the hard global constraint): unchanged in shape,
        // routes straight into the outer `result` with no rounds/history.
        runLegacyPass(result);
        return result;
    }

    // P2 Task 4 (design p2-design.md §4.1-§4.4; plan Global Constraint 9): negotiated rip-up/reroute,
    // cost_driven=true ONLY. Outer loop of kNegotiationRounds full rip-up rounds; each round routes
    // every bundle from scratch (fresh usage/soft_feedthrough_nets, channels' GEOMETRY reused from
    // the single extractChannels call above) into a round-local RoutingResult, using the T2/T3
    // cost-driven Dijkstra + adaptive chunking (unchanged) plus a per-resource history surcharge that
    // persists ACROSS rounds (history_channel/history_ft, declared call-local above routeProblem's
    // return). Round 0's history is all-zero (nothing has been bumped yet), so round 0 is exactly the
    // T3 router in nets-descending bundle order (task requirement 8) -- the ONLY way round 0 differs
    // from a plain T3 call is bundle ORDER (nets-descending here vs `problem.bundles` insertion order
    // for a direct T3 call), which is itself a requirement (§4.1), not a regression.
    std::vector<int> order(problem.bundles.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return problem.bundles[a].nets > problem.bundles[b].nets;
    });

    bool have_best = false;
    RoutingResult best;
    // V5 tail-aware re-pricing state: the previous round's global overflow nets (0.0 before any
    // round has completed, so round 0's price is untouched). Updated right after each round's
    // computeOverflowAndMismatch -- a pure function of that round's own deterministic result, so
    // the re-priced rounds stay deterministic (no iteration-order dependence).
    double prev_overflow_nets = 0.0;
    // Lexicographic round cost (design §4.1/requirement 5; B4 design §7.2 re-denomination): (open_routes,
    // then the SAME units eval.cpp's exact scorer charges -- alpha*WL + kBetaLin*A*R +
    // kBetaTail*A*max(0,R-threshold) + kBetaFt*mismatch, via the shared roundCostOfficial helper
    // (route_cost.hpp). This is the load-bearing re-denomination (design §7.2): the old
    // kOverflowAreaWeight/kFtMismatchWeight(1e6-area) key could rank a routing the golden re-scorer
    // would score WORSE than legacy; roundCostOfficial ranks exactly as the scorer ranks, so the gate's
    // "regression is impossible" guarantee (used for both round selection below and the legacy-vs-
    // cost-driven gate further down) is now actually scorer-faithful.
    // B4.1 (route-pricing portfolio, b4-report.md): official (roundCostOfficial) vs the RETAINED
    // pre-B4 (HEAD 207d08c) 1e6-area-denominated key -- the false branch is byte-identical to that
    // history.
    auto roundCost = [&](const RoutingResult& rr) {
        if (problem.route_pricing_official) return roundCostOfficial(rr, problem.alpha, outline_area);
        double area_cost = problem.alpha * rr.total_wirelength + kOverflowAreaWeight * rr.channel_overflow +
                            kFtMismatchWeight * rr.feedthrough_mismatch;
        return std::pair<int, double>(rr.open_routes, area_cost);
    };

    for (int round = 0; round < kNegotiationRounds; ++round) {
        // Decay at the START of each round (round >= 1), design §4.2: standard PathFinder hygiene so
        // a resource that is no longer congested relaxes back instead of staying permanently
        // poisoned by an earlier round's spike. Round 0 has no decay step (nothing accumulated yet).
        if (round >= 1) {
            for (auto& h : history_channel) {
                h[0] *= kHistoryDecay;
                h[1] *= kHistoryDecay;
            }
            for (auto& h : history_ft) h *= kHistoryDecay;
        }

        // V5 tail-aware re-pricing (Codex V5-port review finding #2; analysis doc section 6):
        // rounds >= 1 price overflow at the CURRENT global overflow level -- the previous round's
        // channel_overflow_nets against the fixed raw capacity -- via roundOverflowPrice
        // (route_cost.hpp): the linear derivative while (O+1)/C <= threshold (bit-identical to
        // the kBetaLin*A/C expression p_ovf already holds, so linear-regime calls are
        // byte-identical to the pre-refinement router), the exact theta-entry secant on a
        // straddle, and the exact AVERAGE P(R)/O past the threshold (a full-rip-up round rebuilds
        // its overflow from zero, so the average -- not the endpoint marginal -- is the price
        // whose round total reproduces the true concave penalty; see the helper's doc). Once a
        // round lands past the threshold, the next round's Dijkstra pricing and history
        // surcharges (both denominated in p_ovf) follow that true tail price instead of the flat
        // linear rate. Round selection and the legacy comparison gate already judge full
        // roundCostOfficial (exact piecewise), unchanged. Guarded on route_pricing_official: the
        // retained pre-B4 portfolio leg never reads p_ovf, and keeping the guard makes that leg's
        // byte-identity trivially self-evident.
        if (round >= 1 && problem.route_pricing_official) {
            p_ovf = roundOverflowPrice(prev_overflow_nets, raw_capacity_total, outline_area);
        }

        // Full rip-up: fresh per-round routing state. Channels' RECTANGLE geometry is extracted once
        // (outside this loop, `result.channels` above) and reused; only the usage counters reset to
        // 0 via a fresh copy. soft_feedthrough_nets/routes/accumulators all start empty/zero.
        RoutingResult round_result;
        round_result.channels = result.channels;  // reuse rect geometry, usage already 0 on a copy
        round_result.soft_feedthrough_nets.assign(problem.blocks.size(), 0.0);
        round_result.ft_area_sum = base_ft_area_sum;  // F-FT: same running-aggregate seed as `result`

        for (int idx : order) {
            const Bundle& bundle = problem.bundles[idx];
            // P2 Task 3 (design p2-design.md §5.2): greedy split-at-bottleneck, unchanged from T3
            // except `findRoutePath`/`commitRoutePath`/`pathSlack` now take `round_result` explicitly
            // and the Dijkstra edge weights (inside findRoutePath) read this round's history vectors.
            double min_chunk = kMinChunkFor(bundle.nets);
            int remaining = bundle.nets;
            int chunks_emitted = 0;
            while (remaining > 0 && chunks_emitted < kMaxChunks) {
                Route probe = findRoutePath(round_result, bundle, remaining);
                if (probe.node_path.empty()) {
                    probe.source = bundle.source;
                    probe.target = bundle.target;
                    probe.nets = remaining;
                    round_result.routes.push_back(probe);
                    break;
                }
                bool last_slot = (chunks_emitted == kMaxChunks - 1);
                int n;
                if (last_slot) {
                    n = remaining;
                } else {
                    double slack = pathSlack(round_result, probe.node_path);
                    if (slack > 0.0) {
                        double clamped = std::max(min_chunk, std::min(slack, static_cast<double>(remaining)));
                        n = std::max(1, std::min(remaining, static_cast<int>(std::llround(clamped))));
                    } else {
                        n = std::min(remaining, kChunkCeil);
                    }
                }
                Route route = commitRoutePath(round_result, bundle, probe.node_path, n);
                round_result.routes.push_back(route);
#ifdef EFGR_ROUTE_TRACE
                {
                    std::string path_desc;
                    for (int nd : probe.node_path) {
                        if (nd < n_blocks) path_desc += problem.blocks[nd].name;
                        else path_desc += round_result.channels[nd - n_blocks].name;
                        path_desc += " ";
                    }
                    double sl = last_slot ? -1.0 : pathSlack(round_result, probe.node_path);
                    std::fprintf(stderr, "[TRACE]   commit bundle(src=%d,nets=%d) chunk#%d n=%d rem_before=%d minchunk=%.1f slack_after=%.1f via [%s]\n",
                                 bundle.source, bundle.nets, chunks_emitted, n, remaining, min_chunk, sl, path_desc.c_str());
                }
#endif
                remaining -= n;
                ++chunks_emitted;
            }
        }

        computeOverflowAndMismatch(round_result);
        prev_overflow_nets = round_result.channel_overflow_nets;

#ifdef EFGR_ROUTE_TRACE
        {
            std::fprintf(stderr, "[TRACE] round %d: overflow=%.6f mismatch=%.6f open=%d cost=%.6f\n",
                         round, round_result.channel_overflow, round_result.feedthrough_mismatch,
                         round_result.open_routes, roundCost(round_result).second);
            for (std::size_t c = 0; c < round_result.channels.size(); ++c) {
                const auto& ch = round_result.channels[c];
                double hcap = 25.0 * std::max(1.0, ch.rect.h);
                double vcap = 25.0 * std::max(1.0, ch.rect.w);
                std::fprintf(stderr, "[TRACE]   %s rect(%.1f,%.1f,%.1f,%.1f) hu=%.1f/%.1f vu=%.1f/%.1f histH=%.4f histV=%.4f\n",
                             ch.name.c_str(), ch.rect.x, ch.rect.y, ch.rect.w, ch.rect.h,
                             ch.horizontal_usage, hcap, ch.vertical_usage, vcap,
                             history_channel[c][0], history_channel[c][1]);
            }
        }
#endif

        // Best-round selection (design §4.1/requirement 5): strictly-less replaces, ties keep the
        // EARLIER round (so a later round identical in cost to an earlier one does NOT overwrite it).
        if (!have_best || roundCost(round_result) < roundCost(best)) {
            best = round_result;
            have_best = true;
        }

        // Bump history AFTER this round, for every resource that ended the round over capacity
        // (design §4.2). Track whether ANY bump was applied this round: if none was, every resource
        // is within capacity and history is unchanged from before this round. Two cases (T4 review
        // correction -- the prior "provably identical" claim only held for the first of these): if
        // history was ALSO all-zero going into this round, every subsequent round prices identically
        // and therefore routes identically (same weights -> same Dijkstra decisions -> same result) --
        // provably identical. If history was NONZERO (a prior round bumped it, then decayed) but this
        // round still ended bump-free, later rounds are provably NOT BETTER for `best`: `best` already
        // holds this round's overflow-free, mismatch-free result, and every later round only ever
        // decays history further (a re-bump requires going over capacity again) -- so a later round
        // can only reproduce the same zero-overflow routing (a tie, and best-round selection above
        // already keeps the EARLIER round on ties) or, once the surcharge decays enough to stop
        // deterring a previously-avoided saturated resource, reintroduce overflow (strictly worse).
        // Either way it cannot beat `best`, so continuing is wasted work in both cases. This keeps
        // clean/overflow-free cases (e.g. case0) at ~1x routing cost instead of paying
        // kNegotiationRounds x, which matters once T6 flips cost_driven on for every exact eval (task
        // requirement 6).
        //
        // V5 re-pricing caveat: a bump-free round also carries ZERO overflow, so a follow-up round
        // would be priced back at the linear rate; if THIS round ran at a tail price (the previous
        // round overflowed past the threshold), the "provably identical" half of the argument above
        // no longer strictly applies -- a follow-up would explore under a different p_ovf and could
        // land on a different trade-off. Breaking anyway is deliberate: `best` already holds this
        // round's overflow-free result and is only ever REPLACED by a strictly better round on the
        // same exact key, so the break forgoes at most a marginal improvement and never regresses --
        // the same trade the pre-refinement router (which also broke here) already made.
        bool any_bump = false;
        for (std::size_t c = 0; c < round_result.channels.size(); ++c) {
            const auto& ch = round_result.channels[c];
            double hcap = 25.0 * std::max(1.0, ch.rect.h);
            double vcap = 25.0 * std::max(1.0, ch.rect.w);
            if (ch.horizontal_usage > hcap) {
                history_channel[c][0] += kHistoryBump * (ch.horizontal_usage - hcap) / hcap;
                any_bump = true;
            }
            if (ch.vertical_usage > vcap) {
                history_channel[c][1] += kHistoryBump * (ch.vertical_usage - vcap) / vcap;
                any_bump = true;
            }
        }
        for (std::size_t b = 0; b < round_result.soft_feedthrough_nets.size(); ++b) {
            double nets = round_result.soft_feedthrough_nets[b];
            if (nets <= 0.0 || !isSoft(problem.blocks[b].type)) continue;
            if (problem.ft_pricing_aggressive) {
                // F-FT: the per-block required>placed criterion below prices per-block mismatch,
                // which in aggressive mode would surcharge FT hops the official V5 aggregate charges
                // nothing for (Codex finding #1). Bump only when the round's AGGREGATE
                // (round_result.ft_area_sum, already fully recomputed by computeOverflowAndMismatch
                // above) exceeds outline_area -- matching what the aggressive Dijkstra pricing itself
                // charges -- and by a uniform, deterministic amount shared by every FT-carrying block
                // this round, proportional to how far over the aggregate is.
                if (round_result.ft_area_sum > outline_area) {
                    history_ft[b] += kHistoryBump * (round_result.ft_area_sum - outline_area) / outline_area;
                    any_bump = true;
                }
                continue;
            }
            double base = problem.blocks[b].ft_base_area > 0.0 ? problem.blocks[b].ft_base_area : problem.blocks[b].area;
            double required_area = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[b], nets)));
            double placed_area = round2(placement.rects[b].w * placement.rects[b].h);
            if (placed_area > 0.0 && required_area > placed_area) {
                history_ft[b] += kHistoryBump * (required_area - placed_area) / placed_area;
                any_bump = true;
            }
        }

        if (!any_bump) break;  // provably-identical early exit, see comment above
    }

    // P2 Task 5.1 (design p2-design.md §9 T5 preamble; controller amendment -- the binding design text
    // mandates a FULL comparison gate, not just an open-routes fallback: "routeProblem returns the
    // cost-driven result when it is fully routed and no worse, else the fallback"). Run the legacy
    // two-pass router UNCONDITIONALLY here -- exactly ONCE (this is routeProblem's only runLegacyPass
    // call site under cost_driven=true, so T5's original open-routes fallback and this gate share one
    // legacy run rather than double-running it; `result` already carries the channel geometry extracted
    // at the top of this call and has not been mutated since, so it is the correct receptacle) -- and
    // pick whichever of {best, result} wins the SAME lexicographic key already used for round selection
    // above: (open_routes, then roundCostOfficial's alpha*WL + kBetaLin*A*R + kBetaTail*A*max(0,R-
    // threshold) + kBetaFt*mismatch -- B4 design §7.2's scorer-faithful re-denomination).
    // `findRoutePath`'s reachability gates are identical between cost_driven's single unified pass
    // (allow_soft always true) and legacy pass 1 (allow_soft true), so open_routes can never be LOWER
    // for legacy than for a genuinely-unroutable cost_driven bundle -- but the FULL key (not just the
    // open_routes component) can still favor legacy when cost_driven is fully routed yet its greedy/
    // history-priced choices land on a worse total cost, which is exactly the gap this amendment closes
    // over the original open-routes-only fallback. Tie rule (deterministic, no ambiguity): `<=` prefers
    // `best` on an EXACT tie -- ties go to the cost-driven result per controller resolution #1, since a
    // tie means cost-driven is provably no worse and preferring it is the whole point of P2. Runtime:
    // this doubles routing cost for every cost_driven=true call (~1x extra legacy pass on top of the
    // negotiation rounds), accepted against the measured 45-300x contest-budget headroom in exchange for
    // the design's "regression is impossible by the comparison gate" guarantee (§9 ordering rationale).
    runLegacyPass(result);
    if (roundCost(best) <= roundCost(result)) {
        return best;
    }
    result.used_fallback = true;
    return result;
}

}  // namespace efgr
