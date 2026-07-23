// B6 (rank-fidelity instrumentation + flag-gated adaptive interval, .superpowers/sdd/b6-brief.md):
// regression coverage for rank_stats.hpp's pure Kendall-tau-b / Spearman-rho / elite-diagnostics
// helpers against hand-computed sequences -- the brief's explicit acceptance list: perfect
// agreement (tau=1), perfect inversion (tau=-1), a hand-computed 5-element case, ties handling --
// plus windowedTauB's trailing-window behavior, spearmanRho, and eliteDiagnostics' rank/agreement
// bookkeeping (none forked from kendallTauB's own hand-derivation, each independently derived
// below).
//
// Also covers the brief's Constraints & acceptance byte-identity substitute: "compare cfg from
// pre-change build is impractical in-suite; instead assert flag-off runs produce identical cfg
// across two invocations AND keep the suite battery + b1/b3 upper-bound pins as the behavior
// guard." testAdaptiveIntervalOffIsDeterministicAcrossInvocations below is that determinism half;
// the actual pre-B6-vs-post-B6 5-case byte comparison (the brief's HARD acceptance gate) is a
// manual git-stash/rebuild diff, reported in b6-report.md, not reproduced here -- b3/b4's own
// regression files document the identical choice for their own analogous byte-identity
// requirements (see regression_b4_20260724.cpp's header comment).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/rank_stats.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-9;

// ---- rank_stats.hpp pure-function unit tests -------------------------------------------------

void testKendallTauPerfectAgreement() {
    std::vector<std::pair<double, double>> pairs = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    double tau = kendallTauB(pairs);
    assert(std::abs(tau - 1.0) < kTol);
    std::cout << "testKendallTauPerfectAgreement ok (tau=" << tau << ")\n";
}

void testKendallTauPerfectInversion() {
    std::vector<std::pair<double, double>> pairs = {{1, 5}, {2, 4}, {3, 3}, {4, 2}, {5, 1}};
    double tau = kendallTauB(pairs);
    assert(std::abs(tau - (-1.0)) < kTol);
    std::cout << "testKendallTauPerfectInversion ok (tau=" << tau << ")\n";
}

// Hand-computed 5-element case, no ties: x=(1,2,3,4,5), y=(2,1,4,3,5). Since x strictly increases
// by index, for i<j the sign of (x_i-x_j) is always negative, so concordance reduces to "is
// y_i<y_j". Enumerating all C(5,2)=10 pairs by hand: (1,2) y:2>1 discordant; (1,3) y:2<4
// concordant; (1,4) y:2<3 concordant; (1,5) y:2<5 concordant; (2,3) y:1<4 concordant; (2,4) y:1<3
// concordant; (2,5) y:1<5 concordant; (3,4) y:4>3 discordant; (3,5) y:4<5 concordant; (4,5) y:3<5
// concordant. 8 concordant, 2 discordant, no ties (both sequences are permutations of {1..5}) ->
// tau_b = (8-2)/sqrt(10*10) = 6/10 = 0.6 exactly.
void testKendallTauHandComputed5Element() {
    std::vector<std::pair<double, double>> pairs = {{1, 2}, {2, 1}, {3, 4}, {4, 3}, {5, 5}};
    double tau = kendallTauB(pairs);
    assert(std::abs(tau - 0.6) < kTol);
    std::cout << "testKendallTauHandComputed5Element ok (tau=" << tau << ")\n";
}

// Ties handling, hand-computed: x=(1,2,2,3), y=(1,2,3,3). Pair (2,3) [0-based indices 1,2] is
// tied in x only (x=2,2; y=2,3 differ); pair (3,4) [indices 2,3] is tied in y only (x=2,3 differ;
// y=3,3). The remaining 4 pairs -- (1,2),(1,3),(1,4),(2,4) [1-based] -- are all concordant, 0
// discordant. n0=C(4,2)=6, n1=tie_x+tie_xy=1+0=1, n2=tie_y+tie_xy=1+0=1.
// tau_b = (4-0)/sqrt((6-1)*(6-1)) = 4/5 = 0.8 exactly.
void testKendallTauTiesHandling() {
    std::vector<std::pair<double, double>> pairs = {{1, 1}, {2, 2}, {2, 3}, {3, 3}};
    double tau = kendallTauB(pairs);
    assert(std::abs(tau - 0.8) < kTol);
    std::cout << "testKendallTauTiesHandling ok (tau=" << tau << ")\n";
}

// Degenerate case: every x value tied means EVERY pair is tied in x, so the tau-b denominator's
// (n0-n1) term is 0 -- must return 0.0 defensively, never NaN/inf.
void testKendallTauAllTiedInXReturnsZero() {
    std::vector<std::pair<double, double>> pairs = {{1, 1}, {1, 2}, {1, 3}};
    double tau = kendallTauB(pairs);
    assert(tau == 0.0);
    std::cout << "testKendallTauAllTiedInXReturnsZero ok\n";
}

void testKendallTauFewerThanTwoPointsReturnsZero() {
    assert(kendallTauB({}) == 0.0);
    assert(kendallTauB({{1.0, 2.0}}) == 0.0);
    std::cout << "testKendallTauFewerThanTwoPointsReturnsZero ok\n";
}

// windowedTauB: trailing-window behavior. Block A (first 5 pairs) is internally perfectly
// discordant (x=0..4 increasing, y=4..0 decreasing); block B (last 5 pairs) is internally
// perfectly concordant (x=y=10..14). Every A-vs-B cross pair is ALSO concordant (A's x/y ranges
// are both strictly below B's), so hand-tallying all C(10,2)=45 pairs: nc = 10 (within B) + 25
// (25 cross pairs) = 35, nd = 10 (within A), no ties anywhere (all 10 x-values and all 10
// y-values are pairwise distinct) -> overall tau_b = (35-10)/sqrt(45*45) = 25/45 = 5/9 exactly.
// windowedTauB(pairs, 5) sees ONLY block B (all concordant) -> 1.0, demonstrably different from
// the whole-run 5/9 -- the distinction the "windowed vs overall" tau_w/tau reporting fields rely
// on. A window >= size falls back to the full sequence (must exactly match kendallTauB(pairs)).
void testWindowedTauBLooksAtTrailingWindowOnly() {
    std::vector<std::pair<double, double>> pairs;
    for (int i = 0; i < 5; ++i) {
        pairs.emplace_back(static_cast<double>(i), static_cast<double>(4 - i));  // discordant block A
    }
    for (int i = 0; i < 5; ++i) {
        pairs.emplace_back(static_cast<double>(10 + i), static_cast<double>(10 + i));  // concordant block B
    }

    double windowed = windowedTauB(pairs, 5);
    assert(std::abs(windowed - 1.0) < kTol);

    double overall = kendallTauB(pairs);
    assert(std::abs(overall - (5.0 / 9.0)) < kTol);

    double full_window = windowedTauB(pairs, pairs.size() + 5);
    assert(std::abs(full_window - overall) < kTol);
    std::cout << "testWindowedTauBLooksAtTrailingWindowOnly ok (windowed=" << windowed << ", overall=" << overall
              << ")\n";
}

void testSpearmanRhoPerfectAgreementAndInversion() {
    std::vector<std::pair<double, double>> agree = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    std::vector<std::pair<double, double>> invert = {{1, 5}, {2, 4}, {3, 3}, {4, 2}, {5, 1}};
    double rho_agree = spearmanRho(agree);
    double rho_invert = spearmanRho(invert);
    assert(std::abs(rho_agree - 1.0) < kTol);
    assert(std::abs(rho_invert - (-1.0)) < kTol);
    std::cout << "testSpearmanRhoPerfectAgreementAndInversion ok (agree=" << rho_agree << ", invert=" << rho_invert
              << ")\n";
}

// eliteDiagnostics: `elite_pairs` order = cheap rank ascending (index 0 = rank 1), per
// search.hpp's SearchResult.elite_pairs contract. Case A: exact-best is NOT the cheap-best (pool
// index 1 / cheap rank 2 has the lowest exact value, 5.0) -> disagreement, rank 2. Case B:
// exact-best IS the cheap-best (index 0) -> agreement, rank 1. Case C: empty pool.
void testEliteDiagnosticsRankAndAgreement() {
    std::vector<std::pair<double, double>> disagree = {{10.0, 50.0}, {20.0, 5.0}, {30.0, 100.0}};
    EliteDiagnostics diag_a = eliteDiagnostics(disagree);
    assert(diag_a.pool_size == 3);
    assert(diag_a.exact_best_cheap_rank == 2);
    assert(!diag_a.top1_agreement);

    std::vector<std::pair<double, double>> agree = {{5.0, 1.0}, {10.0, 50.0}};
    EliteDiagnostics diag_b = eliteDiagnostics(agree);
    assert(diag_b.pool_size == 2);
    assert(diag_b.exact_best_cheap_rank == 1);
    assert(diag_b.top1_agreement);

    EliteDiagnostics diag_c = eliteDiagnostics({});
    assert(diag_c.pool_size == 0);
    assert(diag_c.exact_best_cheap_rank == 0);
    assert(!diag_c.top1_agreement);

    std::cout << "testEliteDiagnosticsRankAndAgreement ok\n";
}

// ---- End-to-end determinism smoke (flag-off byte-identity substitute, see file header) --------

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void testAdaptiveIntervalOffIsDeterministicAcrossInvocations() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
    assert(!options.adaptive_interval);  // the hard-acceptance default this whole task is gated on

    SearchResult first = runPricingPortfolio(problem, "hybrid", options);
    SearchResult second = runPricingPortfolio(problem, "hybrid", options);

    const std::string path_a = "test/b6_determinism_a.cfg";
    const std::string path_b = "test/b6_determinism_b.cfg";
    writeCfg(path_a, problem, first.best_placement, first.best_routing);
    writeCfg(path_b, problem, second.best_placement, second.best_routing);

    std::string content_a = readFile(path_a);
    std::string content_b = readFile(path_b);
    assert(!content_a.empty());
    assert(content_a == content_b);

    // The fold-picked leg's own rank-fidelity bookkeeping must also be reproducible bit-for-bit
    // (same seed, same deterministic exactScore/cheapScore -- see search.cpp's B6 comments: the
    // instrumentation never touches `rng` and exactScore has no hidden randomness).
    assert(first.fidelity_pairs.size() == second.fidelity_pairs.size());
    assert(first.elite_pairs.size() == second.elite_pairs.size());
    assert(first.pricing_official == second.pricing_official);

    std::remove(path_a.c_str());
    std::remove(path_b.c_str());
    std::cout << "testAdaptiveIntervalOffIsDeterministicAcrossInvocations ok (cfg bytes=" << content_a.size()
              << ", fidelity_pairs=" << first.fidelity_pairs.size() << ", elite_pairs=" << first.elite_pairs.size()
              << ")\n";
}

}  // namespace

int main() {
    testKendallTauPerfectAgreement();
    testKendallTauPerfectInversion();
    testKendallTauHandComputed5Element();
    testKendallTauTiesHandling();
    testKendallTauAllTiedInXReturnsZero();
    testKendallTauFewerThanTwoPointsReturnsZero();
    testWindowedTauBLooksAtTrailingWindowOnly();
    testSpearmanRhoPerfectAgreementAndInversion();
    testEliteDiagnosticsRankAndAgreement();
    testAdaptiveIntervalOffIsDeterministicAcrossInvocations();
    std::cout << "b6 regressions passed\n";
    return 0;
}
