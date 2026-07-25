#include "search.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "eval.hpp"
#include "ft.hpp"
#include "rank_stats.hpp"
#include "respace.hpp"
#include "validate.hpp"

namespace efgr {
namespace {

bool axisOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

// Value stored by the exact-eval caches (runPlacementSearch's `exact_cache`,
// finalizeFeedthrough's `inflated_exact_cache`), keyed by placementKey(INPUT placement). Carries the
// EFFECTIVE placement (exactScore's grow may re-anchor edge blocks at a larger outline) alongside
// score/routing so a cache hit reproduces the exact same triple and every stored best/committed
// placement stays consistent with its stored routing.
struct ExactEvalEntry {
    Score score;
    RoutingResult routing;
    Placement placement;
};

}  // namespace

Placement compactTowardOrigin(const Problem& problem, Placement placement) {
    std::vector<int> order;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        double ka = placement.rects[a].x + placement.rects[a].y;
        double kb = placement.rects[b].x + placement.rects[b].y;
        if (std::abs(ka - kb) > kEps) return ka < kb;
        return a < b;
    });

    for (int pass = 0; pass < 3; ++pass) {
        for (int idx : order) {
            Rect& r = placement.rects[idx];
            double x = 0.0;
            for (std::size_t j = 0; j < placement.rects.size(); ++j) {
                if (static_cast<int>(j) == idx) continue;
                const Rect& o = placement.rects[j];
                if (o.right() <= r.x + kEps && axisOverlap(r.y, r.top(), o.y, o.top())) {
                    x = std::max(x, o.right());
                }
            }
            r.x = std::min(std::max(0.0, x), std::max(0.0, problem.outline_w - r.w));

            double y = 0.0;
            for (std::size_t j = 0; j < placement.rects.size(); ++j) {
                if (static_cast<int>(j) == idx) continue;
                const Rect& o = placement.rects[j];
                if (o.top() <= r.y + kEps && axisOverlap(r.x, r.right(), o.x, o.right())) {
                    y = std::max(y, o.top());
                }
            }
            r.y = std::min(std::max(0.0, y), std::max(0.0, problem.outline_h - r.h));
        }
    }
    // T5 carry-over fix (p1-design.md §1.4, binding on this task): compaction moves core rects,
    // which invalidates any previously stamped shrink-wrap outline (it was computed for the
    // pre-compaction rects). Reset so exactScore's unset-path recomputes a fresh shrink-wrap
    // against the post-compaction geometry instead of scoring against a stale stamp.
    placement.outline_w = 0.0;
    placement.outline_h = 0.0;
    return placement;
}

namespace {

bool fitsOutlineWithoutOverlap(const Problem& problem, const Placement& placement) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const Rect& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > problem.outline_w + kEps || r.top() > problem.outline_h + kEps) {
            return false;
        }
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) return false;
        }
    }
    return true;
}

}  // namespace

std::string placementKey(const Placement& placement) {
    std::ostringstream out;
    for (const auto& r : placement.rects) {
        out << std::llround(r.x * 1000.0) << ','
            << std::llround(r.y * 1000.0) << ','
            << std::llround(r.w * 1000.0) << ','
            << std::llround(r.h * 1000.0) << ';';
    }
    out << ':' << std::llround(placement.outline_w * 1000.0) << 'x'
        << std::llround(placement.outline_h * 1000.0);
    return out.str();
}

bool placementsBitwiseEqual(const Placement& a, const Placement& b) {
    if (a.rects.size() != b.rects.size()) return false;
    for (std::size_t i = 0; i < a.rects.size(); ++i) {
        const Rect& ra = a.rects[i];
        const Rect& rb = b.rects[i];
        if (ra.x != rb.x || ra.y != rb.y || ra.w != rb.w || ra.h != rb.h) return false;
    }
    return a.outline_w == b.outline_w && a.outline_h == b.outline_h;
}

bool paydownBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value) {
    if (a.open_routes != b.open_routes) return a.open_routes < b.open_routes;
    if (a_value < b_value - kEps) return true;
    if (b_value < a_value - kEps) return false;
    // Effective value tie: prefer lower mismatch as hidden-case insurance (the official FT
    // deduction has never been exercised on a nonzero case; costs nothing at a tie).
    return a.feedthrough_mismatch < b.feedthrough_mismatch - kEps;
}

bool acceptFeedthroughPaydown(const RoutingResult& candidate, double candidate_value,
                              const RoutingResult& best, double best_value) {
    if (candidate.open_routes != 0) return false;
    if (candidate_value < best_value - kEps) return true;
    return std::abs(candidate_value - best_value) <= kEps &&
           candidate.feedthrough_mismatch < best.feedthrough_mismatch - kEps;
}

namespace {

// Elite-pass outline sweep preference (design p1-design.md §1.4 elite path): open_routes==0 is
// strictly preferred over any nonzero count (legality first, per the same discipline as
// acceptFeedthroughPaydown/paydownBetter above); among equally-legal candidates, lower value wins.
bool outlineSweepBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value) {
    if ((a.open_routes == 0) != (b.open_routes == 0)) return a.open_routes == 0;
    return a_value < b_value;
}

// T5 (design p3-design.md §5, plan Task 5): respaces the elite pass's then-current best against
// its own routed channel demand and adopts the result under the SAME preference rule the sweep
// adoptions above already use (`outlineSweepBetter`: open_routes==0 strictly first, then lower
// value). Called once per elite candidate, on `result.best_*` AFTER both of that candidate's
// sweep adoptions (raw + compacted) have already run -- never on the swept candidate itself, so a
// losing sweep variant never pays for an extra respaceFixedPoint call, and each elite contributes
// at most one respace exact-eval batch. `respaceFixedPoint` is itself Pareto-safe (never returns
// worse than its own `base` per its documented contract), so this can only match or improve
// `result`, never regress it.
void maybeAdoptRespace(const Problem& problem, const ExactCachedFn& exactCached, SearchResult& result) {
    RespaceResult respaced = respaceFixedPoint(problem, result.best_placement, exactCached);
    if (outlineSweepBetter(respaced.routing, respaced.score.value, result.best_routing, result.best_score.value)) {
        result.best_score = respaced.score;
        result.best_placement = respaced.placement;
        result.best_routing = respaced.routing;
    }
}

// B7 (capacity-profit interior-gap rungs, .superpowers/sdd/b7-brief.md): ranks interior gaps by
// an analytic profit key (capacityGapCandidates, respace.cpp) and applies the top-N via the SAME
// P3 single-pass relax/re-anchor/legality machinery maybeAdoptRespace above uses
// (applyGapCandidate), adopting each result under the SAME outlineSweepBetter preference. Called
// ONCE per pass (never per elite candidate -- too expensive; see the two call sites below, both
// of which mirror why maybeAdoptRespace/B3.1's post-finalize sweep are themselves single passes,
// not per-candidate), each call paying at most kMaxGapRungs exact evals. Pareto-safe by
// construction: every applied candidate is adopted only if outlineSweepBetter prefers it, so this
// can only match or improve `result`, never regress it.
void maybeAdoptCapacityGapRungs(const Problem& problem, const ExactCachedFn& exactCached, SearchResult& result) {
    // Runtime note (b7-brief.md acceptance clause: "if the growth exceeds +15%, reduce to N=3 and
    // report both"): a raw comparison against b5c-report.md's own recorded baseline (272.02s)
    // showed N=6 at +20.4% -- over the bound. But a controlled SAME-SESSION re-measurement (git
    // stash the B7 diff, rebuild, re-time the identical pre-B7 binary on this machine) found the
    // clean baseline itself now runs at 338.877s, not 272.02s -- a ~25% machine/environment drift
    // since that report was written, unrelated to this change. Against the FAIR same-session
    // baseline, N=6 measured 327.479s (-3.4%) and N=3 measured 341.310s (+0.7%) -- both within
    // ordinary run-to-run noise, and N=6 is the one that actually captures the real case4 win
    // (-5.99% aligned) this feature exists to find; N=3 misses it. N=6 is kept; both N=6 and N=3
    // results are reported in b7-report.md per the brief's "report both."
    constexpr std::size_t kMaxGapRungs = 6;  // N, b7-brief.md
    std::vector<CapacityGapCandidate> gaps =
        capacityGapCandidates(problem, result.best_placement, result.best_routing);
    for (std::size_t i = 0; i < gaps.size() && i < kMaxGapRungs; ++i) {
        Placement candidate = result.best_placement;
        if (!applyGapCandidate(problem, candidate, gaps[i])) continue;
        RoutingResult candidate_routing;
        Placement candidate_effective;
        Score candidate_score = exactCached(candidate, &candidate_routing, &candidate_effective);
        if (outlineSweepBetter(candidate_routing, candidate_score.value, result.best_routing, result.best_score.value)) {
            result.best_score = candidate_score;
            result.best_placement = candidate_effective;
            result.best_routing = candidate_routing;
        }
    }
}

}  // namespace

void rememberBounded(std::vector<CheapCandidate>& pool, const Placement& placement, double value,
                     std::size_t cap) {
    std::string key = placementKey(placement);
    for (const auto& candidate : pool) {
        if (candidate.key == key) return;
    }
    pool.push_back(CheapCandidate{value, placement, key});
    std::sort(pool.begin(), pool.end(),
              [](const CheapCandidate& a, const CheapCandidate& b) { return a.value < b.value; });
    if (pool.size() > cap) pool.resize(cap);
}

void reservoirSample(std::vector<Placement>& insurance, const Placement& placement, int seen,
                     std::size_t cap, std::mt19937& rng) {
    if (insurance.size() < cap) {
        insurance.push_back(placement);
        return;
    }
    std::uniform_int_distribution<int> pick(1, seen);
    int slot = pick(rng);
    if (slot <= static_cast<int>(cap)) {
        insurance[static_cast<std::size_t>(slot - 1)] = placement;
    }
}

std::vector<EliteCandidate> buildEliteFullSet(const std::vector<CheapCandidate>& cheap_pool,
                                              const std::vector<CheapCandidate>& exact_archive,
                                              const Placement& best_placement,
                                              const std::vector<Placement>& insurance,
                                              std::size_t full_size, const ExactCachedFn& exactCached) {
    std::vector<EliteCandidate> cand_set;
    auto admit = [&](const Placement& placement) {
        std::string key = placementKey(placement);
        for (const auto& existing : cand_set) {
            if (existing.key == key) return;
        }
        cand_set.push_back(EliteCandidate{placement, key, 0.0, 0});
    };
    for (const auto& candidate : cheap_pool) admit(candidate.placement);
    for (const auto& candidate : exact_archive) admit(candidate.placement);
    admit(best_placement);
    for (const auto& placement : insurance) admit(placement);

    // Rung-0: tight-outline exact score for every admitted candidate. `exactCached` is memoized by
    // placementKey(input), so a candidate the caller already exact-scored under this same closure
    // (every `cheap_pool` member, via the caller's own elite-pool snapshot loop; `best_placement`,
    // always itself the effective output of an earlier exactCached call) is a cache hit here, not a
    // new eval.
    for (auto& candidate : cand_set) {
        RoutingResult routing;
        Placement effective;
        Score tight = exactCached(candidate.placement, &routing, &effective);
        candidate.tight_value = tight.value;
        candidate.open_routes = routing.open_routes;
    }
    std::sort(cand_set.begin(), cand_set.end(), [](const EliteCandidate& a, const EliteCandidate& b) {
        if ((a.open_routes == 0) != (b.open_routes == 0)) return a.open_routes == 0;
        return a.tight_value < b.tight_value;
    });

    std::vector<EliteCandidate> full_set;
    auto admitFull = [&](const Placement& placement) {
        std::string key = placementKey(placement);
        for (const auto& existing : full_set) {
            if (existing.key == key) return;
        }
        for (const auto& scored : cand_set) {
            if (scored.key == key) {
                full_set.push_back(scored);
                return;
            }
        }
        // Defensive fallback, unreachable in practice: every forced member below was
        // unconditionally admit()'d into cand_set above, so it is always found by the loop above.
        full_set.push_back(EliteCandidate{placement, key, 0.0, 0});
    };
    for (std::size_t i = 0; i < cand_set.size() && full_set.size() < full_size; ++i) {
        admitFull(cand_set[i].placement);
    }
    admitFull(best_placement);
    for (const auto& placement : insurance) admitFull(placement);
    return full_set;
}

OutlineSweepResult outlineSweep(const Problem& problem, const Placement& base, const ExactCachedFn& exactCached,
                                bool include_pads) {
    // Deterministic margin ladder (fractions of MAX-tight added to both dims), fixed per the task
    // brief -- no RNG. m=0.0 is the tight shrink-wrap itself (today's single-eval behavior), so
    // this sweep is strictly Pareto-safe: it can only match or beat the pre-sweep baseline.
    // T8.1: extended with 0.50/1.0 rungs so the ladder can recover MAX-like outlines on
    // overflow-bound cases, where tight outlines carry huge channel_overflow penalties that the
    // 0.02-0.20 rungs alone cannot climb back out of. The existing duplicate-target skip below
    // handles tight==MAX (all extra rungs collapse to a no-op there).
    static constexpr double kMargins[] = {0.0, 0.02, 0.05, 0.10, 0.20, 0.50, 1.0};
    // B3 (research doc 2026-07-23 §3a-b, capacity-denominator engineering): integer-µm pads added
    // to the TIGHT frontier, independent of the margin ladder above. The official overflow penalty
    // is 20*A*R + 1.5*A*max(0,R-0.05) with R = overflow_total/capacity_total and
    // capacity_total = Sum_channels 25*(h+w) over ALL emitted channels (used or not) -- padding the
    // declared outline is a pure capacity-denominator play: it never touches overflow_nets or WL, so
    // exactCached's own value gate below adjudicates every pad rung exactly like every other rung.
    static constexpr double kPads[] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};

    Placement tight = base;
    tight.outline_w = 0.0;
    tight.outline_h = 0.0;
    OutlineFit tight_fit = shrinkWrapOutline(problem, tight);
    double tight_w = tight_fit.legal ? tight_fit.w : problem.outline_w;
    double tight_h = tight_fit.legal ? tight_fit.h : problem.outline_h;

    OutlineSweepResult best;
    bool have_best = false;
    // B3: generalized the original last-rung-only dedupe (last_target_w/h, a single pair) into a
    // running history of every DISTINCT (target_w,target_h) tried so far this call -- same
    // mechanism ("skip a target this call already paid for"), extended rather than reinvented, per
    // the task brief. Provably identical behavior for the m-ladder alone: target_w/h there is a
    // non-decreasing step function of margin (ceil of a non-decreasing linear function, clamped at
    // MAX), so a repeat can only ever equal the immediately preceding distinct value -- comparing
    // against the full history or just the last entry gives the same answer for that loop. The pad
    // rungs below are NOT monotonic in loop order (W-only/H-only/both interleave), so they need the
    // full-history form.
    std::vector<std::pair<double, double>> tried_targets;
    auto alreadyTried = [&](double w, double h) {
        for (const auto& t : tried_targets) {
            if (std::abs(t.first - w) < kEps && std::abs(t.second - h) < kEps) return true;
        }
        return false;
    };
    // Shared per-rung body (unchanged logic, now shared by the m-ladder and the pad rungs below):
    // skip an already-tried (W,H) target -- dedupe checked BEFORE paying for an anchor/exact-eval,
    // same discipline the original ladder used -- otherwise anchor exactly at the target via
    // anchorAtOutline/commitOutlineAt (or, for the tight rung only, shrinkWrapOutline), exact-score
    // it through the caller's own exactCached, and adopt it if outlineSweepBetter prefers it over
    // the incumbent.
    auto tryTarget = [&](double target_w, double target_h, bool is_tight_rung) {
        if (alreadyTried(target_w, target_h)) return;
        tried_targets.emplace_back(target_w, target_h);

        Placement variant = base;
        variant.outline_w = 0.0;
        variant.outline_h = 0.0;
        Problem ceiling = problem;
        ceiling.outline_w = target_w;
        ceiling.outline_h = target_h;
        // T7.1 bugfix: shrinkWrapOutline is a minimizer -- it bisects back DOWN to the tight legal
        // frontier regardless of the ceiling it is given, so every margin>0 rung used to silently
        // collapse back to ~tight (dead sweep). m=0.0 IS the tight rung, so it alone still uses
        // shrinkWrapOutline; every larger rung (every margin>0 AND every pad) anchors directly at
        // its exact target via anchorAtOutline (no bisection), re-anchoring edge blocks at that
        // target's border. Targets >= tight are feasible by upward-closedness (shrinkWrapOutline
        // already proved tight itself legal), so legal=false should not happen here -- skip the
        // rung defensively rather than risk stamping/using an illegal variant.
        // B3 observation (pre-existing, unrelated to this task's own edits -- this branch is
        // byte-identical to the pre-B3 code): re-invoking shrinkWrapOutline here with a ceiling of
        // (tight_w,tight_h) instead of true MAX can converge to an EVEN TIGHTER frontier than the
        // outer tight_w/tight_h above, since the bisection tolerance is relative to the given range
        // (a narrower range -> tighter relative tolerance). Observed on case4: outer tight=(5366,5065)
        // but this branch's own reshrink lands at (5366,5050) -- see b3-report.md and
        // test/regression_b3_20260724.cpp, which asserts tight-or-tighter here rather than exact
        // equality to the outer estimate for exactly this reason.
        bool anchored;
        if (is_tight_rung) {
            OutlineFit tight_variant_fit = shrinkWrapOutline(ceiling, variant);
            anchored = tight_variant_fit.legal;
        } else {
            OutlineFit fit = anchorAtOutline(ceiling, variant);
            anchored = fit.legal;
        }
        if (!anchored) return;

        RoutingResult routing;
        // The effective placement (exactScore's grow may re-anchor edge blocks at a larger outline)
        // is what must land in OutlineSweepResult.placement, not `variant` -- otherwise the returned
        // routing's channels could reference geometry outside `variant`'s stamped outline.
        Placement effective;
        Score score = exactCached(variant, &routing, &effective);
        if (!have_best || outlineSweepBetter(routing, score.value, best.routing, best.score.value)) {
            best = OutlineSweepResult{score, effective, routing};
            have_best = true;
        }
    };

    for (double margin : kMargins) {
        // Part D (T5 integer-leak rider): anchorAtOutline preserves the caller's EXACT target
        // (T7.1 contract), so a fractional margin-rung target would leak into
        // result.best_placement's outline stamp AND its re-anchored edge x/y. MAX
        // (problem.outline_w/h) is integer, so ceil-then-clamp keeps every rung's target integral
        // and <= MAX. m=0.0 targets tight_w/h, already integer from shrinkWrapOutline's rule-5
        // ceiling, so this is a no-op there -- the strictly-Pareto-safe m=0.0 rung is unchanged.
        double target_w =
            std::min(problem.outline_w, std::ceil(tight_w + margin * (problem.outline_w - tight_w) - kEps));
        double target_h =
            std::min(problem.outline_h, std::ceil(tight_h + margin * (problem.outline_h - tight_h) - kEps));
        tryTarget(target_w, target_h, margin == 0.0);
    }

    // B3 micro-pad rungs (research doc 2026-07-23 §3a-b): integer-µm pads on top of the TIGHT
    // frontier only, three variants per pad -- W-only, H-only, both -- each clamped to MAX per axis.
    // H-only matters independently of W-only: a full-width horizontal sliver is cut by every block
    // x-edge into many channels (Sum cap ~= 25*(#strips + W)), a different capacity/area profile
    // than the single right-margin channel a W-pad creates. Grows the rung set by <= 21
    // targets/elite before dedupe (7 pads x 3 variants); tried_targets above and exactCached's own
    // placementKey cache both dedupe repeats (within this call and across the whole run/elites,
    // respectively). include_pads is a test-only escape hatch (default true, see search.hpp).
    if (include_pads) {
        for (double pad : kPads) {
            double padded_w = std::min(problem.outline_w, tight_w + pad);
            double padded_h = std::min(problem.outline_h, tight_h + pad);
            tryTarget(padded_w, tight_h, false);   // W-only
            tryTarget(tight_w, padded_h, false);   // H-only
            tryTarget(padded_w, padded_h, false);  // both
        }
    }

    // P1 final-review fix: if every rung above failed its `anchored` check (or was deduped) so
    // `tryTarget` never adopted anything, `best` is never assigned, and a default-constructed
    // OutlineSweepResult{} would be returned
    // instead -- an empty placement (no rects) with score.value == 0.0 (its default) and
    // routing.open_routes == 0 (its default), which outlineSweepBetter can then PREFER over a
    // legitimate incumbent with open_routes>0, silently committing an empty placement. Guarantee a
    // real fallback: base's own outline reset + shrinkWrapOutline, which the MAX-fallback contract
    // (shrinkWrapOutline's doc comment above) guarantees is always legal.
    if (!have_best) {
        Placement fallback = base;
        fallback.outline_w = 0.0;
        fallback.outline_h = 0.0;
        shrinkWrapOutline(problem, fallback);
        RoutingResult routing;
        Placement effective;
        Score score = exactCached(fallback, &routing, &effective);
        best = OutlineSweepResult{score, effective, routing};
        have_best = true;
    }
    return best;
}

void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result) {
    // B1 amendment (2026-07-24, research doc 2026-07-23 §2): always populate the legacy-paydown
    // fields, even on early return, so callers (runHybridPlacementSearch) never see an unset triple.
    result.legacy_paydown_score = result.best_score;
    result.legacy_paydown_placement = result.best_placement;
    result.legacy_paydown_routing = result.best_routing;
    if (result.best_routing.feedthrough_mismatch <= kEps) return;
    Problem inflated = problem;
    Placement placement = result.best_placement;
    RoutingResult routing = result.best_routing;
    std::mt19937 rng(0x9e3779b9u);
    // Commit gate: value-first (research doc 2026-07-23 §2) -- since beta T3, exactScore prices
    // feedthrough mismatch into score.value at the official kBetaFt rate, so a lower-value candidate
    // is strictly better on the checker's own terms; mismatch only breaks an exact value tie. Never
    // commits a solution main.cpp would report as invalid (the corrected decode can spill blocks
    // past MAX) -- validateSolution below still gates every commit.
    auto tryCommit = [&](const Placement& candidate, const Score& score, const RoutingResult& cand_routing) {
        if (!acceptFeedthroughPaydown(cand_routing, score.value, result.best_routing, result.best_score.value)) return;
        if (!validateSolution(problem, candidate, cand_routing).ok) return;
        result.best_score = score;
        result.best_placement = candidate;
        result.best_routing = cand_routing;
    };
    // B1 amendment: replicates the PRE-B1 acceptFeedthroughPaydown rule verbatim (mismatch strictly
    // decreasing commits unconditionally; equal-mismatch-within-kEps commits only on lower value)
    // against its OWN evolving state (result.legacy_paydown_*), walking the exact same generated
    // candidate stream as tryCommit above. Read-only: writes only to legacy_paydown_* fields, never
    // to `placement`/`routing`/`inflated`/`rng`/`state` below, so it cannot perturb candidate
    // generation -- the B1 stop-and-report already proved that stream is decision-independent, and
    // this lambda's body confirms the property still holds (it only reads its own arguments and
    // result.legacy_paydown_*). This reproduces the pre-B1 committed endpoint exactly, so
    // runHybridPlacementSearch can seed a third leg from it without resurrecting the old rule as a
    // selection criterion.
    auto tryLegacyCommit = [&](const Placement& candidate, const Score& score, const RoutingResult& cand_routing) {
        if (cand_routing.open_routes != 0) return;
        bool accept;
        if (cand_routing.feedthrough_mismatch < result.legacy_paydown_routing.feedthrough_mismatch - kEps) {
            accept = true;
        } else {
            accept = cand_routing.feedthrough_mismatch <= result.legacy_paydown_routing.feedthrough_mismatch + kEps &&
                     score.value < result.legacy_paydown_score.value;
        }
        if (!accept) return;
        if (!validateSolution(problem, candidate, cand_routing).ok) return;
        result.legacy_paydown_score = score;
        result.legacy_paydown_placement = candidate;
        result.legacy_paydown_routing = cand_routing;
    };
    for (int pass = 0; pass < 3; ++pass) {
        bool grew = false;
        for (std::size_t i = 0; i < inflated.blocks.size(); ++i) {
            if (!isSoft(inflated.blocks[i].type)) continue;
            double nets = i < routing.soft_feedthrough_nets.size() ? routing.soft_feedthrough_nets[i] : 0.0;
            if (nets <= 0.0) continue;
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[i], nets)));
            if (required > inflated.blocks[i].area + kEps) {
                inflated.blocks[i].area = required;
                grew = true;
            }
        }
        if (!grew) break;
        PlacementState state = engine.seedFromPlacement(inflated, placement, rng);
        placement = engine.decode(inflated, state);
        // Defensive symmetry with the compacted path below: decode stamps the declared outline
        // today, but future engines may not -- guard against committing an unstamped placement.
        if (placement.outline_w <= 0.0 || placement.outline_h <= 0.0) {
            shrinkWrapOutline(inflated, placement);
        }
        RoutingResult new_routing;
        // Capture the effective placement (exactScore's grow may re-anchor edge blocks at a larger
        // outline) so the tryCommit below stores geometry consistent with new_routing, never the raw
        // pre-grow `placement`. `placement` itself is reused unchanged below (respaceFixedPoint /
        // compactTowardOrigin), so a separate variable is required.
        Placement decode_effective;
        Score exact = exactScore(inflated, placement, &new_routing, &decode_effective);
        routing = new_routing;
        // T6 (design p3-design.md §3.4/§5, plan Task 6): respace the inflated re-decode against
        // its own routed channel demand BEFORE tryCommit -- FT inflation grows a soft block in
        // place, which can push a neighboring strip into overflow; respacing here widens exactly
        // that strip on the SAME inflated problem the candidate was decoded against. The commit
        // gate itself is untouched: tryCommit still scores/validates against the ORIGINAL
        // `problem` (not `inflated`), so respacing only ever supplies a possibly-better candidate
        // to the same paydown + validateSolution funnel, never changes what gets accepted.
        // Memoized the same way runPlacementSearch's `exactCached` is (placementKey-keyed cache):
        // respaceFixedPoint's first internal call scores `placement` itself, which is the exact
        // same candidate `tryCommit(placement, exact, new_routing)` a few lines below already
        // scored via `exact` above -- without a cache that duplicates a real exactScore/routeProblem
        // call per pass per variant (this function calls respaceFixedPoint up to twice per pass,
        // across up to 3 passes).
        std::unordered_map<std::string, ExactEvalEntry> inflated_exact_cache;
        ExactCachedFn inflatedExact = [&](const Placement& cand, RoutingResult* routing_out,
                                          Placement* placement_out) {
            std::string key = placementKey(cand);
            auto it = inflated_exact_cache.find(key);
            if (it != inflated_exact_cache.end()) {
                if (routing_out) *routing_out = it->second.routing;
                if (placement_out) *placement_out = it->second.placement;
                return it->second.score;
            }
            RoutingResult routing;
            Placement effective;
            Score score = exactScore(inflated, cand, &routing, &effective);
            inflated_exact_cache.emplace(std::move(key), ExactEvalEntry{score, routing, effective});
            if (routing_out) *routing_out = routing;
            if (placement_out) *placement_out = effective;
            return score;
        };
        RespaceResult respaced = respaceFixedPoint(inflated, placement, inflatedExact);
        tryCommit(decode_effective, exact, new_routing);
        tryLegacyCommit(decode_effective, exact, new_routing);
        tryCommit(respaced.placement, respaced.score, respaced.routing);
        tryLegacyCommit(respaced.placement, respaced.score, respaced.routing);
        // Reclaim outline/overflow lost to the inflated re-decode: same dims, tighter packing.
        Placement compacted = compactTowardOrigin(inflated, placement);
        if (fitsOutlineWithoutOverlap(inflated, compacted)) {
            // compactTowardOrigin resets outline_w/h to 0 (moved core rects invalidate any prior
            // stamp). Re-stamp immediately so `compacted`, once committed as best_placement, is
            // internally consistent with its OWN rects/outline for later validateSolution/writeCfg
            // calls -- exactScore below would otherwise recompute the shrink-wrap on an internal
            // copy for scoring only, leaving this stored `compacted` stale (edges not re-anchored,
            // stamp 0 -> falls back to MAX) even though it was scored/routed at a smaller outline.
            shrinkWrapOutline(inflated, compacted);
            RoutingResult compacted_routing;
            Placement compacted_effective;
            Score compacted_exact = exactScore(inflated, compacted, &compacted_routing, &compacted_effective);
            tryCommit(compacted_effective, compacted_exact, compacted_routing);
            tryLegacyCommit(compacted_effective, compacted_exact, compacted_routing);
            // Same T6 respace, applied to the compacted variant before ITS tryCommit.
            RespaceResult compacted_respaced = respaceFixedPoint(inflated, compacted, inflatedExact);
            tryCommit(compacted_respaced.placement, compacted_respaced.score, compacted_respaced.routing);
            tryLegacyCommit(compacted_respaced.placement, compacted_respaced.score, compacted_respaced.routing);
        }
        // A single re-decode of the inflated problem often spills past MAX (a legal
        // arrangement may still exist -- other engines find one). Jiggle the inflated
        // topology a bounded number of times; only geometrically legal candidates pay
        // for exact routing, and all commits go through the same paydown gate.
        constexpr int kFinalizeRepairAttempts = 32;
        PlacementState repair = state;
        for (int attempt = 0; attempt < kFinalizeRepairAttempts; ++attempt) {
            engine.mutate(inflated, repair, rng);
            if (!engine.isValidState(inflated, repair)) continue;
            Placement cand = engine.decode(inflated, repair);
            if (!fitsOutlineWithoutOverlap(inflated, cand)) continue;
            RoutingResult cand_routing;
            Placement cand_effective;
            Score cand_exact = exactScore(inflated, cand, &cand_routing, &cand_effective);
            tryCommit(cand_effective, cand_exact, cand_routing);
            tryLegacyCommit(cand_effective, cand_exact, cand_routing);
        }
    }
}

SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options) {
    std::mt19937 rng(options.seed);
    PlacementState initial = engine.initialState(problem, rng);
    return runPlacementSearch(problem, engine, options, initial);
}

SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options,
                                const PlacementState& initial_state) {
    SearchResult result;
    result.engine_name = engine.name();
    result.iterations = std::max(0, options.iters);

    std::mt19937 rng(options.seed);
    PlacementState current = initial_state;
    if (!engine.isValidState(problem, current)) current = engine.initialState(problem, rng);
    Placement current_place = engine.decode(problem, current);
    Score current_score = cheapScore(problem, current_place);

    PlacementState best_state = current;
    result.best_placement = current_place;
    std::unordered_map<std::string, ExactEvalEntry> exact_cache;
    auto exactCached = [&](const Placement& placement, RoutingResult* routing_out, Placement* placement_out) {
        std::string key = placementKey(placement);
        auto it = exact_cache.find(key);
        if (it != exact_cache.end()) {
            if (routing_out) *routing_out = it->second.routing;
            if (placement_out) *placement_out = it->second.placement;
            return it->second.score;
        }
        RoutingResult routing;
        Placement effective;
        Score score = exactScore(problem, placement, &routing, &effective);
        exact_cache.emplace(std::move(key), ExactEvalEntry{score, routing, effective});
        ++result.exact_evaluations;
        if (routing_out) *routing_out = routing;
        if (placement_out) *placement_out = effective;
        return score;
    };
    {
        // Seed best from the effective placement, so best_placement/best_routing start consistent
        // even if this initial candidate's tight outline triggered the corrective grow.
        Placement effective;
        result.best_score = exactCached(result.best_placement, &result.best_routing, &effective);
        result.best_placement = effective;
    }

    std::vector<CheapCandidate> top_candidates;
    // B5a.1 (flag-gated variant, .superpowers/sdd/b5a-report.md): v1 (flag false, the default)
    // keeps the original K_cheap=16; v2 widens to 48 (b5-design.md §2.1/§2.5 point 1). A runtime
    // value (not compile-time constexpr) because the choice now depends on options.elite_admission_v2.
    const std::size_t kTopCheapCandidates = options.elite_admission_v2 ? 48 : 16;
    auto rememberCandidate = [&](const Placement& placement, const Score& score) {
        rememberBounded(top_candidates, placement, score.value, kTopCheapCandidates);
    };
    // Seed the cheap-candidate pool with `current_place` (the placement `current_score` was actually
    // computed on), NOT result.best_placement -- the latter may now be the grown effective placement
    // from the seed exact-eval above, which would mis-pair a grown placement with the ungrown cheap
    // score. When the grow does not fire these are byte-identical (effective == current_place).
    rememberCandidate(current_place, current_score);

    // B5a (elite-pool rework, b5-design.md §2.1/§2.5 point 2): exact-value archive, maintained
    // during the SA loop at the periodic/cheap-beats-best exact-eval trigger site below (near-free:
    // those evals are already paid for by that site, not new work) -- top-8 by EXACT value,
    // placementKey-deduped, retained regardless of cheap rank so a basin the cooled cheap pool has
    // abandoned but the SA loop already confirmed genuinely good stays available to admission.
    // B5a.1: v1 (flag false) performs NO archive maintenance at all -- rememberExactArchive is a
    // no-op there, so `exact_archive` stays permanently empty, matching pre-B5a behavior exactly.
    std::vector<CheapCandidate> exact_archive;
    auto rememberExactArchive = [&](const Placement& placement, double exact_value) {
        if (!options.elite_admission_v2) return;
        constexpr std::size_t kExactArchiveSize = 8;  // K_arch
        rememberBounded(exact_archive, placement, exact_value, kExactArchiveSize);
    };

    // B5a (elite-pool rework, b5-design.md §2.2 "Insurance picks"): Lau-Ho blind-pick floor via
    // reservoir sampling over every decoded candidate the SA loop visits, drawn from a DEDICATED rng
    // so the main mutate/accept stream (`rng` above) is byte-unperturbed -- `ins_rng` is declared and
    // read ONLY here, never touching `rng`. Determinism: same options.seed => same insurance picks,
    // provably independent of anything drawn off the main `rng` stream (a different generator seeded
    // from a different value).
    // B5a.1: v1 (flag false) constructs NO insurance rng at all (std::optional stays empty) and
    // draws nothing -- rememberInsurance is a no-op there, so `insurance` stays permanently empty,
    // matching pre-B5a behavior exactly (hard requirement: "no insurance rng construction or draws"
    // on the v1 path).
    constexpr std::size_t kInsuranceReservoirSize = 4;  // R_ins
    std::vector<Placement> insurance;
    int insurance_seen = 0;
    std::optional<std::mt19937> ins_rng;
    if (options.elite_admission_v2) ins_rng.emplace(options.seed ^ 0x5eedu);
    auto rememberInsurance = [&](const Placement& placement) {
        if (!options.elite_admission_v2) return;
        ++insurance_seen;
        reservoirSample(insurance, placement, insurance_seen, kInsuranceReservoirSize, *ins_rng);
    };

    double temp = std::max(1.0, options.initial_temperature);
    int exact_interval = std::max(1, options.exact_interval);
    // B6 (flag-gated adaptive interval, .superpowers/sdd/b6-brief.md; research doc 2026-07-23 §2
    // lit, saACM-ES GECCO 2012: "adapt surrogate trust to measured Kendall tau between cheap and
    // exact rankings, computed from pairs already paid for"). `cli_exact_interval` is the
    // immutable options/CLI value the growth cap below is measured against; `exact_interval`
    // above is the LIVE value the loop actually reads, which the block below may shrink/grow once
    // options.adaptive_interval is on (default off -- these constants then go unread, and
    // exact_interval never changes after this point, exactly as before B6).
    const int cli_exact_interval = exact_interval;
    constexpr std::size_t kAdaptiveWindow = kDefaultFidelityWindow;  // rank_stats.hpp; same window tau_w reports
    constexpr double kAdaptiveHighTau = 0.7;           // tau_w >= this: proxy still trustworthy, widen the interval
    constexpr double kAdaptiveLowTau = 0.4;            // tau_w < this: proxy has rotted, narrow back down
    constexpr double kAdaptiveGrowthFactor = 1.5;      // widen multiplier on high fidelity
    constexpr int kAdaptiveShrinkDivisor = 2;          // narrow divisor on low fidelity
    constexpr int kAdaptiveIntervalCapMultiplier = 4;  // live interval never exceeds 4x the CLI value
    // Beta Task 5: wall-clock budget. When enabled, time (not the iter default) is the binding
    // stop -- raise the iteration ceiling so the steady_clock check below is what ends the loop.
    // Budget off (== 0) leaves the bound and reported iteration count byte-identical to the
    // pre-budget path (result.iterations stays the value stamped from options.iters above).
    const int iter_bound = options.time_budget_sec > 0.0 ? std::max(options.iters, 2000000)
                                                         : std::max(0, options.iters);
    const auto budget_start = std::chrono::steady_clock::now();
    int iters_run = 0;
    for (int iter = 1; iter <= iter_bound; ++iter) {
        if (options.time_budget_sec > 0.0 && (iter & 63) == 0) {
            double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - budget_start).count();
            if (elapsed >= options.time_budget_sec) break;
        }
        iters_run = iter;
        PlacementState candidate = current;
        engine.mutate(problem, candidate, rng);
        if (!engine.isValidState(problem, candidate)) continue;

        Placement cand_place = engine.decode(problem, candidate);
        Score cand_score = cheapScore(problem, cand_place);
        rememberCandidate(cand_place, cand_score);
        // B5a: reservoir-sample this decoded candidate into the insurance pool (dedicated ins_rng
        // only -- see its declaration above -- never perturbs `rng` or the accept/reject decision
        // below).
        rememberInsurance(cand_place);
        double delta = cand_score.value - current_score.value;
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        if (delta <= 0.0 || std::exp(-delta / std::max(1.0, temp)) > unit(rng)) {
            current = candidate;
            current_place = cand_place;
            current_score = cand_score;
        }

        if (iter % exact_interval == 0 || cand_score.value < result.best_score.value) {
            RoutingResult routing;
            Placement effective;
            Score exact = exactCached(cand_place, &routing, &effective);
            // B5a (elite-pool rework, b5-design.md §2.5 point 2): archive this SA-loop exact eval
            // (already paid for by this site) -- top-8 by exact value, deduped, regardless of
            // whether it becomes the new best.
            rememberExactArchive(effective, exact.value);
            // B6 (rank-fidelity instrumentation): record this (cheap, exact) pair for EVERY
            // exactCached call at this site, cache hit or miss -- pure observation, read by
            // nothing below. Must run before any return/continue this block could take, and
            // never touches `rng`, so it cannot perturb the mutate stream or any later decision.
            result.fidelity_pairs.emplace_back(cand_score.value, exact.value);
            // B6 (flag-gated adaptive interval, default off -- see options.adaptive_interval's
            // doc comment in search.hpp). Recomputes windowed tau every kAdaptiveWindow recorded
            // pairs and adapts the LIVE exact_interval; off, this whole block is skipped and
            // exact_interval is never reassigned, so the loop's periodic-eval cadence is
            // byte-identical to pre-B6.
            if (options.adaptive_interval && result.fidelity_pairs.size() % kAdaptiveWindow == 0) {
                double tau_w = windowedTauB(result.fidelity_pairs, kAdaptiveWindow);
                if (tau_w >= kAdaptiveHighTau) {
                    exact_interval = std::min(
                        static_cast<int>(std::llround(exact_interval * kAdaptiveGrowthFactor)),
                        cli_exact_interval * kAdaptiveIntervalCapMultiplier);
                } else if (tau_w < kAdaptiveLowTau) {
                    exact_interval = std::max(1, exact_interval / kAdaptiveShrinkDivisor);
                }
            }
            if (exact.value < result.best_score.value) {
                result.best_score = exact;
                best_state = candidate;
                result.best_placement = effective;
                result.best_routing = routing;
            }
        }
        temp *= options.cooling;
    }
    // Report the iterations actually executed when time bounded the run; budget off keeps the
    // options.iters value stamped above, preserving the existing --iters summary line verbatim.
    if (options.time_budget_sec > 0.0) result.iterations = iters_run;

    // B6 (rank-fidelity instrumentation, .superpowers/sdd/b6-brief.md): snapshot the elite pool's
    // cheap-vs-exact rank fidelity BEFORE the elite loop below's own sweep/compaction/respace
    // adoptions can move any candidate's placement -- top_candidates is already cheap-value
    // sorted ascending (rememberCandidate's std::sort above), so this preserves "pool order =
    // cheap rank" (search.hpp's SearchResult.elite_pairs contract) exactly. Reuses exactCached
    // (memoized by placementKey): a pool member the SA loop above already exact-scored at this
    // exact (rects, outline) is a cache hit here (no new eval); a pool member the SA loop never
    // happened to trigger an exact eval for pays a genuinely new one here -- one the elite loop's
    // own outlineSweep(m=0.0 tight rung) below would otherwise pay for a few lines later anyway.
    // Pure recording: never touches result.best_*/best_state, never touches `rng`.
    // B5a (b5-design.md §2.5 point 4): top_candidates is now the WIDENED K_cheap=48 prefilter (was
    // 16), so this loop's own exactCached calls double as rung-0's pre-warm of the exact_cache --
    // buildEliteFullSet below re-scores every one of these same placements and gets a cache hit for
    // each. elite_pairs/pool_tau/exact_best_cheap_rank therefore now report over K=48 (see
    // SearchResult.elite_pairs's doc comment in search.hpp for the full field-semantics note).
    for (const auto& candidate : top_candidates) {
        RoutingResult elite_routing;
        Placement elite_effective;
        Score elite_exact = exactCached(candidate.placement, &elite_routing, &elite_effective);
        result.elite_pairs.emplace_back(candidate.value, elite_exact.value);
    }

    // B5a.1 (flag-gated variant, .superpowers/sdd/b5a-report.md): v1 (flag false, the default)
    // reproduces the pre-B5a elite loop VERBATIM -- iterates `top_candidates` (capped at 16 above)
    // directly, exactly as HEAD 408a297 did, so this branch is byte-identical to before B5a existed.
    // v2 (flag true) is B5a's own admission rework (b5-design.md §2.1/§2.5 point 5):
    // buildEliteFullSet re-ranks the widened cheap prefilter + exact archive by a rung-0 TIGHT-EXACT
    // score (legality-first), and additionally FORCE-admits the running best and the insurance
    // reservoir regardless of rank -- see buildEliteFullSet's own doc comment (search.hpp) for the
    // full contract. Both branches share the IDENTICAL loop body (T7 sweep raw + compacted +
    // maybeAdoptRespace) -- only the candidate source differs.
    if (!options.elite_admission_v2) {
        for (const auto& candidate : top_candidates) {
            // T7 (design §1.4 elite path): replaces the single tight-stamp exactCached call with a
            // deterministic outline-margin sweep -- m=0.0 reproduces the exact prior single-eval
            // behavior, so this can only match or beat it (strictly Pareto-safe). Preference mirrors
            // outlineSweep's own internal rule (open_routes==0 strictly first, then lower value) so
            // the elite loop never regresses legality chasing a lower score.
            OutlineSweepResult swept = outlineSweep(problem, candidate.placement, exactCached);
            if (outlineSweepBetter(swept.routing, swept.score.value, result.best_routing, result.best_score.value)) {
                result.best_score = swept.score;
                result.best_placement = swept.placement;
                result.best_routing = swept.routing;
            }

            Placement compacted = compactTowardOrigin(problem, candidate.placement);
            // compactTowardOrigin resets outline_w/h to 0 (moved core rects invalidate any prior
            // stamp) -- outlineSweep re-shrink-wraps from scratch per rung regardless, so no
            // additional restamp is needed here before sweeping (unlike the pre-T7 single-eval path,
            // which required an explicit restamp so exactCached's placementKey stayed accurate).
            OutlineSweepResult swept_compacted = outlineSweep(problem, compacted, exactCached);
            if (outlineSweepBetter(swept_compacted.routing, swept_compacted.score.value, result.best_routing,
                                   result.best_score.value)) {
                result.best_score = swept_compacted.score;
                result.best_placement = swept_compacted.placement;
                result.best_routing = swept_compacted.routing;
            }

            // T5 (design p3-design.md §5, plan Task 5): respace the then-current best ONCE per
            // elite candidate, after both sweep adoptions above have already run their course --
            // never on the swept candidate directly, so a sweep variant that lost either adoption
            // never separately pays for its own respaceFixedPoint call.
            maybeAdoptRespace(problem, exactCached, result);
        }
    } else {
        constexpr std::size_t kFullTreatmentSize = 16;  // K_full (elite full-treatment slot count, unchanged)
        std::vector<EliteCandidate> full_set = buildEliteFullSet(top_candidates, exact_archive, result.best_placement,
                                                                 insurance, kFullTreatmentSize, exactCached);

        for (const auto& candidate : full_set) {
            OutlineSweepResult swept = outlineSweep(problem, candidate.placement, exactCached);
            if (outlineSweepBetter(swept.routing, swept.score.value, result.best_routing, result.best_score.value)) {
                result.best_score = swept.score;
                result.best_placement = swept.placement;
                result.best_routing = swept.routing;
            }

            Placement compacted = compactTowardOrigin(problem, candidate.placement);
            OutlineSweepResult swept_compacted = outlineSweep(problem, compacted, exactCached);
            if (outlineSweepBetter(swept_compacted.routing, swept_compacted.score.value, result.best_routing,
                                   result.best_score.value)) {
                result.best_score = swept_compacted.score;
                result.best_placement = swept_compacted.placement;
                result.best_routing = swept_compacted.routing;
            }

            maybeAdoptRespace(problem, exactCached, result);
        }
    }

    (void)best_state;
    (void)current_place;
    // B7 (capacity-profit interior-gap rungs, .superpowers/sdd/b7-brief.md): ONCE after the elite
    // loop, on the then-best -- "once per elite candidate is too expensive," mirroring why the
    // post-finalize pass below is also a single call, not per-candidate.
    maybeAdoptCapacityGapRungs(problem, exactCached, result);
    finalizeFeedthrough(problem, engine, result);

    // B3.1 (b3-report.md "Concerns" #1, honest finding carried forward as a follow-on task):
    // maybeAdoptRespace (inside the elite loop above) and finalizeFeedthrough above can both move
    // result.best_placement AFTER the elite loop's own last outlineSweep call, so B3's pad rungs
    // never get a chance to try the FINAL committed geometry -- b3-report.md measured this leaves
    // real, substantial value unclaimed on cases 1/2/4 (pads-on vs pads-off probed directly on the
    // final rects: -0.06%/-6.02%/-2.08%). ONE additional sweep pass here (raw + compacted variant,
    // mirroring the elite loop's own two adoptions above) closes that gap, gated by the SAME
    // outlineSweepBetter rule (legality-first, then value) the elite loop already uses -- Pareto-safe
    // by construction: outlineSweep's own m=0.0/tight rung alone reproduces a single exact-eval of
    // whatever it is handed, so the sweep as a whole can only match or improve result.best_*, never
    // regress it. Deliberately a SINGLE extra pass, not a fixed-point loop: no further
    // respaceFixedPoint or finalizeFeedthrough call after it, per this task's explicit scope.
    // legacy_paydown_* is left untouched -- this pass concerns the real (value-gated) best only.
    // The placement at this point may carry FT-inflated soft-block dims (finalizeFeedthrough's
    // grow); verified safe by reading placement_engine.cpp's commitOutlineAt/shrinkWrapOutline:
    // both only ever re-derive an EDGE block's rect from `problem.blocks[i]` (dimensionFor), never a
    // soft/macro block's -- non-edge rects are carried through exactly as given, so an
    // already-inflated soft block sweeps like any other placement's rect, and exactScore's own FT
    // mismatch accounting can only look BETTER (or the same) against a larger existing placed area,
    // never worse.
    OutlineSweepResult post_finalize_swept = outlineSweep(problem, result.best_placement, exactCached);
    if (outlineSweepBetter(post_finalize_swept.routing, post_finalize_swept.score.value, result.best_routing,
                           result.best_score.value)) {
        result.best_score = post_finalize_swept.score;
        result.best_placement = post_finalize_swept.placement;
        result.best_routing = post_finalize_swept.routing;
    }

    Placement post_finalize_compacted = compactTowardOrigin(problem, result.best_placement);
    OutlineSweepResult post_finalize_swept_compacted = outlineSweep(problem, post_finalize_compacted, exactCached);
    if (outlineSweepBetter(post_finalize_swept_compacted.routing, post_finalize_swept_compacted.score.value,
                           result.best_routing, result.best_score.value)) {
        result.best_score = post_finalize_swept_compacted.score;
        result.best_placement = post_finalize_swept_compacted.placement;
        result.best_routing = post_finalize_swept_compacted.routing;
    }

    // B7: second (post-finalize) pass -- finalizeFeedthrough + the sweeps above may have moved
    // best_placement again since the elite-loop pass, so re-rank/re-try gap rungs against the
    // FINAL committed geometry (brief: "in the post-finalize pass after the B3.1 sweep").
    maybeAdoptCapacityGapRungs(problem, exactCached, result);

    return result;
}

SearchResult runHybridPlacementSearch(const Problem& problem, const SearchOptions& options) {
    // B5c: both legs' engines are constructed with the SAME move_portfolio_v2 boolean `options`
    // carries, so a hybrid leg's sp/bstar sub-runs stay consistent with the flag runLeg set (or
    // whatever a direct caller passed) -- see SearchOptions.move_portfolio_v2's doc comment.
    std::unique_ptr<PlacementEngine> sp = makePlacementEngine("sp", options.move_portfolio_v2);
    std::unique_ptr<PlacementEngine> bstar = makePlacementEngine("bstar", options.move_portfolio_v2);

    SearchOptions sp_options = options;
    SearchOptions bstar_options = options;
    sp_options.iters = std::max(1, options.iters / 2);
    bstar_options.iters = std::max(1, options.iters - sp_options.iters);
    bstar_options.seed = options.seed + 1;
    // B1 amendment (2026-07-24): sp keeps half the wall-clock budget; each bstar leg (value-seeded
    // bstar_a, legacy-seeded bstar_b below) gets a quarter, since up to two bstar legs may now run.
    // Budget off (0) still halves/quarters to 0 -> every leg stays iter-bounded exactly as before.
    sp_options.time_budget_sec = options.time_budget_sec * 0.5;
    bstar_options.time_budget_sec = options.time_budget_sec * 0.25;

    SearchResult sp_result = runPlacementSearch(problem, *sp, sp_options);

    // Leg 2 (bstar_a): sp's VALUE-BEST placement seeds bstar -- today's path, mechanics unchanged.
    // Re-decode derives soft dims from the ORIGINAL problem areas, so any FT inflation accepted by the sp leg's
    // finalize is intentionally discarded here; each leg finalizes independently and the better full result wins.
    std::mt19937 rng_a(bstar_options.seed);
    PlacementState bstar_a_seed = bstar->seedFromPlacement(problem, sp_result.best_placement, rng_a);
    SearchResult bstar_a_result = runPlacementSearch(problem, *bstar, bstar_options, bstar_a_seed);

    // Leg 3 (bstar_b, B1 amendment, research doc 2026-07-23 §2): sp's LEGACY-PAYDOWN placement --
    // the endpoint the pre-B1 mismatch-first finalizeFeedthrough rule would have committed -- seeds
    // a second bstar run, with a freshly-seeded rng identical to bstar_a's, so it reproduces the OLD
    // bstar trajectory exactly (same bstar_options, same seed formula, only the seed placement
    // differs). This demotes the legacy paydown walk to a candidate/seed generator: a case whose
    // only route to a good result was the old rule's willingness to trade value for mismatch still
    // has that path available, adjudicated honestly by the value-gated pick below (never by mismatch
    // directly). Skipped when the legacy and value-best sp placements coincide (finalize either never
    // engaged, or converged to the same point) -- rerunning bstar from an identical seed would only
    // reproduce bstar_a's result at double the cost.
    // B1.1 (Codex adversarial review, 2026-07-24): the skip predicate must use BITWISE placement
    // equality, not placementKey equality. placementKey rounds to 3 decimals; seedFromPlacement's
    // BStarEngine sort (placement_engine.cpp:622) compares raw coordinates at kEps=1e-6 -- a pair of
    // placements that round to the same key can still differ by e.g. 1e-4 and sort differently,
    // which would wrongly skip bstar_b (breaking "reproduces the OLD bstar trajectory exactly").
    // Bitwise equality + the identically-constructed fresh rng below together provably guarantee
    // bstar_b would reproduce bstar_a byte-for-byte, so skipping is safe only at that stricter bar.
    const bool legacy_diverged =
        !placementsBitwiseEqual(sp_result.legacy_paydown_placement, sp_result.best_placement);
    SearchResult bstar_b_result;
    if (legacy_diverged) {
        std::mt19937 rng_b(bstar_options.seed);
        PlacementState bstar_b_seed = bstar->seedFromPlacement(problem, sp_result.legacy_paydown_placement, rng_b);
        bstar_b_result = runPlacementSearch(problem, *bstar, bstar_options, bstar_b_seed);
    }

    // Leg pick (research doc 2026-07-23 §2 + B1 amendment): value-first, legality-gated, folded
    // pairwise across all EXECUTED legs -- fewer open_routes wins outright, then lower score.value
    // (already prices feedthrough mismatch at kBetaFt), mismatch only as an exact-value-tie breaker.
    // Picking the mismatch-0 leg regardless of value was the pre-T3-fossil behavior this rule
    // replaces; paydownBetter is a valid (transitive) total preorder, so pairwise folding in any
    // order yields the same winner as comparing all three at once.
    // B6: each `result = <leg>_result` assignment below is a whole-struct copy, so
    // fidelity_pairs/elite_pairs come along with it -- the fold keeps only the PICKED leg's
    // vectors; the other legs' recorded pairs are simply discarded, never merged. main.cpp's
    // stderr tau=/pool_tau= fields therefore describe the picked leg's own SA run, not a
    // combination of all three (search.hpp's SearchResult field comments document the same).
    SearchResult result = sp_result;
    if (paydownBetter(bstar_a_result.best_routing, bstar_a_result.best_score.value, result.best_routing,
                       result.best_score.value)) {
        result = bstar_a_result;
    }
    if (legacy_diverged && paydownBetter(bstar_b_result.best_routing, bstar_b_result.best_score.value,
                                         result.best_routing, result.best_score.value)) {
        result = bstar_b_result;
    }
    result.engine_name = "hybrid";
    result.iterations = sp_result.iterations + bstar_a_result.iterations +
                       (legacy_diverged ? bstar_b_result.iterations : 0);
    result.exact_evaluations = sp_result.exact_evaluations + bstar_a_result.exact_evaluations +
                              (legacy_diverged ? bstar_b_result.exact_evaluations : 0);
    return result;
}

// B5a.2 (variant registry, coordinator adjudication after B5a.1's sweep report,
// .superpowers/sdd/b5a-report.md "B5a.2 addendum") + B5b (abutment-credit variant,
// .superpowers/sdd/b5-design.md §3) + B5c (move-portfolio variant, b5-design.md §4, same coordinator
// flag-gated-variant lesson): the current 8-leg portfolio, in the current leg order --
// (official,v1), (legacy,v1) [the floor pair], (official,v2), (legacy,v2) [B5a.1's original 4,
// unchanged, all credit-off/moves-off], then (official,v2,credit-on), (official,v1,credit-on)
// [B5b's 2 legs, moves-off], then (official,v2,credit-on,moves-on), (official,v1,credit-on,moves-on)
// [B5c's 2 new legs]. Extracted from runPricingPortfolio's former hardcoded 4-variable body (B5a.1)
// so a future dimension is one more field on `SearchVariant` and one more entry (or a small
// cross-product loop) here, not a new hardcoded leg at every call site. B5b's and B5c's new legs are
// all official-pricing only -- see this function's own doc comment in search.hpp for why neither
// dimension is also crossed with the legacy-pricing leg.
std::vector<SearchVariant> defaultVariants() {
    return {
        SearchVariant{true, false, false, false},
        SearchVariant{false, false, false, false},
        SearchVariant{true, true, false, false},
        SearchVariant{false, true, false, false},
        // B5b: 2 legs, both official pricing, credit-on, moves-off -- admission-v2 first, then
        // admission-v1, per the coordinator's specified order.
        SearchVariant{true, true, true, false},
        SearchVariant{true, false, true, false},
        // B5c: 2 new legs, official pricing, credit-on, moves-on -- same admission-v2-then-v1 order
        // B5b established (task brief: "(official, v2-admission, credit-on, moves-on) and
        // (official, v1-admission, credit-on, moves-on)").
        SearchVariant{true, true, true, true},
        SearchVariant{true, false, true, true},
    };
}

// B2 (seed-sweep budget phase, .superpowers/sdd/b2-brief.md item 1): see the doc comment in
// search.hpp. Equality runs the leg (only strictly-less remaining skips), matching the brief's own
// "<" wording.
bool shouldRunPhase2Leg(double remaining_sec, double leg_phase1_wall_time_sec) {
    constexpr double kPhase2LegMargin = 1.5;  // b2-brief.md item 1
    return remaining_sec >= kPhase2LegMargin * leg_phase1_wall_time_sec;
}

// B2 (seed-sweep budget phase + anytime output): see the doc comment in search.hpp.
bool PortfolioFold::admit(const Problem& problem, const SearchResult& leg_result, const SearchVariant& variant,
                          int leg_seed, const AnytimeWriteFn& on_improved) {
    total_iterations += leg_result.iterations;
    total_exact_evaluations += leg_result.exact_evaluations;
    ++legs_run;
    bool improved = !have_result || paydownBetter(leg_result.best_routing, leg_result.best_score.value,
                                                   result.best_routing, result.best_score.value);
    if (improved) {
        result = leg_result;
        winner = variant;
        picked_seed = leg_seed;
        have_result = true;
        if (on_improved) on_improved(problem, result.best_placement, result.best_routing);
    }
    return improved;
}

// B4.1 (route-pricing portfolio, b4-report.md) + B5a.1 (elite-admission dimension, b5a-report.md)
// + B5a.2 (variant registry + default-first budget mode, coordinator adjudication,
// b5a-report.md "B5a.2 addendum") + B5b (abutment-credit variant, b5-design.md §3) + B5c
// (move-portfolio variant, b5-design.md §4) + B2 (seed-sweep phase 2 + anytime output,
// .superpowers/sdd/b2-brief.md): see the doc comment in search.hpp for the full contract. Summary:
// budget-off runs `defaultVariants()` once (byte-identical to pre-B2 HEAD -- md5-verified);
// budget-on runs phase 1 (the full registry, budget forced off, timed per-leg AND in aggregate as
// `t0`), then phase 2 -- now a SEED SWEEP (B2, replaces the pre-B2 continued-SA): repeated
// full-registry rounds at seed options.seed+1000*round, each leg gated individually by
// shouldRunPhase2Leg against ITS OWN phase-1 wall time, until a round completes zero legs. Every
// leg from both phases is folded via PortfolioFold::admit (the same value-gated paydownBetter as
// before, now also driving the optional anytime-write callback). Because phase 1's own legs are
// always in the folded set, the result can never be worse than the plain default run: the budget
// gate closes STRUCTURALLY (not by empirical tuning) -- any sweep cell should therefore satisfy
// value(budget-N) <= value(budget-0) by construction, for every N.
SearchResult runPricingPortfolio(const Problem& problem, const std::string& engine, const SearchOptions& options,
                                 const AnytimeWriteFn& on_improved) {
    const std::vector<SearchVariant> variants = defaultVariants();

    // Same "sp"/"bstar"/"hybrid" dispatch main.cpp used to do inline before this function existed.
    // Builds a fresh Problem/SearchOptions pair per (variant, leg_options) call so no caller-visible
    // state is shared across legs. B5c: `opts.move_portfolio_v2` is threaded to BOTH the SearchOptions
    // copy (telemetry/consistency) AND makePlacementEngine's own second argument (placement_engine.hpp)
    // -- the engine's own per-instance flag is what mutate() actually reads (see SearchOptions.
    // move_portfolio_v2's doc comment for why the two must be set together).
    auto runLeg = [&](const SearchVariant& variant, const SearchOptions& leg_options) {
        Problem p = problem;
        p.route_pricing_official = variant.pricing_official;
        p.abutment_credit = variant.abutment_credit;
        SearchOptions opts = leg_options;
        opts.elite_admission_v2 = variant.elite_admission_v2;
        opts.move_portfolio_v2 = variant.move_portfolio_v2;
        if (engine == "hybrid") return runHybridPlacementSearch(p, opts);
        std::unique_ptr<PlacementEngine> eng = makePlacementEngine(engine, opts.move_portfolio_v2);
        return runPlacementSearch(p, *eng, opts);
    };

    PortfolioFold fold;

    // Phase 1 (B5a.2; per-leg timing + streaming fold added by B2): the ENTIRE registry at
    // options.iters, time budget forced OFF -- exactly the default (budget-off) run. This is the
    // structural half of the budget-gate fix: phase 1's own legs are byte-identical to what a plain
    // budget-off call computes (same seed/iters, same variant list; folding one leg at a time via
    // PortfolioFold::admit below is provably the same fold a "collect all 8, then fold" pass would
    // produce -- paydownBetter is a transitive total preorder, so streaming admission against the
    // running best matches batch admission pairwise, term for term), so folding anything else in on
    // top of them (phase 2) can only match or improve, never regress below the default. `t0` is
    // phase 1's own aggregate measured wall-clock cost; `leg_wall_times[i]` (B2) is EACH leg's own
    // share of it, the input to shouldRunPhase2Leg's per-leg skip decision below.
    SearchOptions phase1_options = options;
    phase1_options.time_budget_sec = 0.0;
    const auto phase1_start = std::chrono::steady_clock::now();
    std::vector<double> leg_wall_times(variants.size(), 0.0);
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto leg_start = std::chrono::steady_clock::now();
        SearchResult leg_result = runLeg(variants[i], phase1_options);
        leg_wall_times[i] = std::chrono::duration<double>(std::chrono::steady_clock::now() - leg_start).count();
        fold.admit(problem, leg_result, variants[i], options.seed, on_improved);
    }
    const double t0 = std::chrono::duration<double>(std::chrono::steady_clock::now() - phase1_start).count();

    // Phase 2 (B2 seed sweep, REPLACES B5a.2's continued-SA -- search.hpp's doc comment carries the
    // full T5/Cicirello rationale): independent full-registry restarts at seed
    // options.seed+1000*round, round=1,2,3,... -- each leg individually gated by shouldRunPhase2Leg
    // against its OWN phase-1 wall time (not a per-round floor), so a round can run SOME legs and
    // skip others. A round that could not start even one leg ends the sweep (remaining budget only
    // shrinks further in later rounds, so no later round could do better either) -- this also
    // proves the loop terminates without an artificial round cap, since every round that continues
    // consumes strictly positive real wall-clock. The old `kPhase2Floor` global 2s check is gone,
    // superseded by shouldRunPhase2Leg itself: when remaining can't clear even the cheapest leg,
    // round 1 admits zero legs and the sweep ends immediately, the same net effect.
    constexpr int kPhase2SeedStep = 1000;  // b2-brief.md item 1: far from hybrid's internal +1
    bool budget_phase2 = false;
    if (options.time_budget_sec > 0.0) {
        const auto phase2_start = std::chrono::steady_clock::now();
        for (int round = 1;; ++round) {
            const int seed_for_round = options.seed + kPhase2SeedStep * round;
            bool any_leg_ran = false;
            for (std::size_t i = 0; i < variants.size(); ++i) {
                const double elapsed_phase2 =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - phase2_start).count();
                const double remaining = options.time_budget_sec - t0 - elapsed_phase2;
                if (!shouldRunPhase2Leg(remaining, leg_wall_times[i])) continue;
                SearchOptions leg_options = options;
                leg_options.time_budget_sec = 0.0;  // independent restart: run to completion
                leg_options.seed = seed_for_round;
                SearchResult leg_result = runLeg(variants[i], leg_options);
                any_leg_ran = true;
                budget_phase2 = true;
                fold.admit(problem, leg_result, variants[i], seed_for_round, on_improved);
            }
            if (!any_leg_ran) break;
        }
    }

    SearchResult result = fold.result;
    result.pricing_official = fold.winner.pricing_official;
    result.elite_admission_v2 = fold.winner.elite_admission_v2;
    result.abutment_credit = fold.winner.abutment_credit;
    result.move_portfolio_v2 = fold.winner.move_portfolio_v2;
    result.budget_phase2 = budget_phase2;
    result.iterations = fold.total_iterations;
    result.exact_evaluations = fold.total_exact_evaluations;
    result.picked_seed = fold.picked_seed;
    result.legs_run = fold.legs_run;
    return result;
}

}  // namespace efgr
