// L6 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md §L6.7): regression
// coverage for mergeUndirectedBundles (io.hpp/io.cpp, pure function) and, once wired in (L6-2..L6-4),
// the --undirected-bundles flag's downstream effects -- the CONN coverage validator check
// (validate.cpp, flag-independent) and the routing/port/FT invariants the design doc proves hold
// under merge. Tests are added incrementally as the corresponding production code lands (see the
// design doc's "合併實作順序" table); each test's own header comment names which design §L6.7 row
// (U1..U8) it covers.
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/validate.hpp"

namespace {
using namespace efgr;

// The 20260727 refresh CSVs are untracked and live only in the main tree (see this task's own
// instructions) -- referenced here by absolute path, never copied into this worktree.
const std::string kNewCaseDir =
    "/home/yyds/Early-Floorplanning-with-Global-Route/test/E_testcase_20260727/testcase-upload-20260727/";

std::string newCasePath(int i) {
    return kNewCaseDir + "2026 ICCAD_case.case" + std::to_string(i) + ".csv";
}

// ---- U1: mergeUndirectedBundles pure-function contract (design §L6.7) ---------------------------

void testU1MergeUndirectedBundlesBasic() {
    // {A->B:300, B->A:571, A->C:5} -> {(A,B,871),(A,C,5)}, ascending by (source,target).
    std::vector<Bundle> input = {
        Bundle{0, 1, 300},  // A->B
        Bundle{1, 0, 571},  // B->A
        Bundle{0, 2, 5},    // A->C
    };
    std::vector<Bundle> merged = mergeUndirectedBundles(input);
    assert(merged.size() == 2);
    assert(merged[0].source == 0 && merged[0].target == 1 && merged[0].nets == 871);
    assert(merged[1].source == 0 && merged[1].target == 2 && merged[1].nets == 5);
    for (const auto& b : merged) assert(b.source < b.target);  // source < target always holds

    // empty input -> empty output
    assert(mergeUndirectedBundles({}).empty());

    // idempotent: merging an already-merged list again is the identity
    std::vector<Bundle> merged_again = mergeUndirectedBundles(merged);
    assert(merged_again.size() == merged.size());
    for (std::size_t i = 0; i < merged.size(); ++i) {
        assert(merged_again[i].source == merged[i].source);
        assert(merged_again[i].target == merged[i].target);
        assert(merged_again[i].nets == merged[i].nets);
    }
    std::cout << "testU1MergeUndirectedBundlesBasic ok\n";
}

// ---- U2: Sigma nets preserved on the 5 new (20260727) CSVs (design §L6.7) -----------------------

void testU2SumPreservedOnNewCsvs() {
    for (int i = 0; i <= 4; ++i) {
        Problem p = parseProblem(newCasePath(i));
        std::vector<Bundle> merged = mergeUndirectedBundles(p.bundles);

        long long parsed_sum = 0;
        for (const auto& b : p.bundles) parsed_sum += b.nets;
        long long merged_sum = 0;
        for (const auto& b : merged) merged_sum += b.nets;
        assert(parsed_sum == merged_sum);

        // per-unordered-pair sums match too, not just the grand total
        std::map<std::pair<int, int>, long long> want;
        for (const auto& b : p.bundles) want[std::minmax(b.source, b.target)] += b.nets;
        std::map<std::pair<int, int>, long long> got;
        for (const auto& b : merged) {
            assert(b.source < b.target);
            got[{b.source, b.target}] += b.nets;
        }
        assert(want == got);
        std::cout << "testU2SumPreservedOnNewCsvs case" << i << " ok (bundles " << p.bundles.size()
                  << " -> " << merged.size() << ", sum=" << parsed_sum << ")\n";
    }
}

// ---- U3: symmetric control -- case1's matrix is exactly symmetric (design §L6.7) ----------------

void testU3SymmetricCase1MatrixHalves() {
    Problem p = parseProblem(newCasePath(1));
    assert(p.bundles.size() == 82);
    std::vector<Bundle> merged = mergeUndirectedBundles(p.bundles);
    assert(merged.size() == 41);
    assert(merged.size() * 2 == p.bundles.size());

    std::map<std::pair<int, int>, int> directed;
    for (const auto& b : p.bundles) directed[{b.source, b.target}] = b.nets;
    for (const auto& b : merged) {
        auto it_fwd = directed.find({b.source, b.target});
        auto it_rev = directed.find({b.target, b.source});
        assert(it_fwd != directed.end() && it_rev != directed.end());
        assert(it_fwd->second == it_rev->second);  // matrix is exactly symmetric for case1
        assert(b.nets == it_fwd->second * 2);
    }
    std::cout << "testU3SymmetricCase1MatrixHalves ok (merged=" << merged.size() << ")\n";
}

// ---- U8: CONN coverage validator check (design §L6.3/§L6.7) -------------------------------------
// Flag-INDEPENDENT (validate.cpp never reads problem.undirected_bundles for this check) -- a
// hand-built RoutingResult that under-routes a bundle by 1 net must fail with a "conn mismatch"
// message, regardless of whether problem.bundles is in its parsed (directed) or merged form.

void testU8ConnValidatorCatchesMissingNet() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    for (int i = 0; i < 2; ++i) {
        Block b;
        b.name = i == 0 ? "BLKA" : "BLKB";
        b.width = 10.0;
        b.height = 10.0;
        b.area = 100.0;
        b.type = BlockType::Macro;
        problem.blocks.push_back(b);
    }
    problem.bundles.push_back(Bundle{0, 1, 100});  // wants 100 nets between BLKA/BLKB

    Placement placement;
    placement.rects.push_back(Rect{0.0, 0.0, 10.0, 10.0});
    placement.rects.push_back(Rect{20.0, 0.0, 10.0, 10.0});

    RoutingResult routing;  // hand-built: only routes 99 of the 100 wanted nets
    Route route;
    route.source = 0;
    route.target = 1;
    route.nets = 99;
    route.open = false;
    route.steps.push_back(RouteStep{"BLKA", 3});
    route.steps.push_back(RouteStep{"BLKB", 1});
    routing.routes.push_back(route);

    ValidationResult vr = validateSolution(problem, placement, routing);
    assert(!vr.ok);
    bool found = false;
    for (const auto& msg : vr.messages) {
        if (msg.find("conn mismatch") != std::string::npos) found = true;
    }
    assert(found);
    std::cout << "testU8ConnValidatorCatchesMissingNet ok\n";
}

}  // namespace

int main() {
    testU1MergeUndirectedBundlesBasic();
    testU2SumPreservedOnNewCsvs();
    testU3SymmetricCase1MatrixHalves();
    testU8ConnValidatorCatchesMissingNet();
    std::cout << "undirected bundle merge regressions passed\n";
    return 0;
}
