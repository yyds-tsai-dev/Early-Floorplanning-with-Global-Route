#include "cfg_score.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace efgr {

namespace {

// Official overflow-penalty parameters, read byte-verified from every alpha report.json's
// `cost_breakdown` block (beta-task-2-report.md, Section 1).
constexpr double kBetaLin = 20.0;    // linear term coefficient
constexpr double kBetaTail = 1.5;    // tail term coefficient (R > threshold)
constexpr double kThreshold = 0.05;  // overflow-rate threshold
constexpr double kCapPerUm = 25.0;   // wire density limit: 25 nets / um

// Checker guiding point for the wirelength CHAIN (NOT guidePoint's overlap midpoint).
// The point sits on the shared border between `a` and `b`; the border is picked by MINIMUM gap so
// the classification is robust to the .cfg's 3-decimal rounding (abutting edges can differ by up to
// ~0.001 um -- report.json flags these as "borderline"). Along the border the coordinate is the
// average of the two rectangles' centers (equivalently, the midpoint of the two facing-edge
// midpoints); across the border it is the midpoint of the two near edges. Verified to reproduce
// total_wirelength to <=1e-6 rel on all 5 alpha cases.
std::pair<double, double> guidingPoint(const Rect& a, const Rect& b) {
    const double g_ar_bx = std::abs(a.right() - b.x);  // a.right meets b.left  (vertical border)
    const double g_br_ax = std::abs(b.right() - a.x);  // b.right meets a.left  (vertical border)
    const double g_at_by = std::abs(a.top() - b.y);    // a.top   meets b.bottom(horizontal border)
    const double g_bt_ay = std::abs(b.top() - a.y);    // b.top   meets a.bottom(horizontal border)
    const double min_vertical = std::min(g_ar_bx, g_br_ax);
    const double min_horizontal = std::min(g_at_by, g_bt_ay);
    if (min_vertical <= min_horizontal) {
        const double x = (g_ar_bx <= g_br_ax) ? 0.5 * (a.right() + b.x) : 0.5 * (b.right() + a.x);
        return {x, 0.5 * (a.cy() + b.cy())};
    }
    const double y = (g_at_by <= g_bt_ay) ? 0.5 * (a.top() + b.y) : 0.5 * (b.top() + a.y);
    return {0.5 * (a.cx() + b.cx()), y};
}

}  // namespace

CfgSolution parseCfg(const std::string& path) {
    CfgSolution cfg;
    std::ifstream in(path);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        line = trim(line);  // strips CR and surrounding whitespace
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        tok = trim(tok);
        if (tok == "OUTLINE") {
            ss >> cfg.outline_w >> cfg.outline_h;
            section.clear();
            continue;
        }
        if (tok == "BLOCK" || tok == "CHANNEL") {
            section = tok;
            continue;
        }
        if (tok == "PATH") {
            std::string rest;
            std::getline(ss, rest);
            rest = trim(rest);
            if (rest.empty()) {          // section header "PATH"
                section = "PATH";
                continue;
            }
            std::istringstream ps(rest);  // data line "PATH <nets> <obj> <edge> ..."
            CfgSolution::Path p;
            ps >> p.nets;
            std::string obj;
            int edge = 0;
            while (ps >> obj >> edge) p.steps.push_back({trim(obj), edge});
            cfg.paths.push_back(std::move(p));
            section = "PATH";
            continue;
        }
        if (tok == "END") {
            section.clear();
            continue;
        }
        if (section == "BLOCK") {
            Rect r;
            ss >> r.x >> r.y >> r.w >> r.h;
            cfg.blocks[tok] = r;
        } else if (section == "CHANNEL") {
            Channel ch;
            ch.name = tok;
            ss >> ch.rect.x >> ch.rect.y >> ch.rect.w >> ch.rect.h;
            cfg.channels.push_back(ch);
        }
    }
    return cfg;
}

OfficialBreakdown officialScoreCfg(const Problem& problem, const CfgSolution& cfg) {
    OfficialBreakdown out;
    out.outline_area = cfg.outline_w * cfg.outline_h;

    // Name lookups. Channels own mutable H/V demand accumulators (per-edge model).
    std::unordered_map<std::string, int> chan_index;
    std::vector<double> hu(cfg.channels.size(), 0.0);
    std::vector<double> vu(cfg.channels.size(), 0.0);
    for (std::size_t k = 0; k < cfg.channels.size(); ++k) chan_index[cfg.channels[k].name] = static_cast<int>(k);

    auto rect_of = [&](const std::string& name, Rect& out_rect) -> bool {
        auto b = cfg.blocks.find(name);
        if (b != cfg.blocks.end()) { out_rect = b->second; return true; }
        auto c = chan_index.find(name);
        if (c != chan_index.end()) { out_rect = cfg.channels[c->second].rect; return true; }
        return false;  // unknown object -> skip (defensive; legal .cfg never hits this)
    };

    double wirelength = 0.0;
    for (const CfgSolution::Path& p : cfg.paths) {
        // (1) Per-edge channel demand from the RAW step list (each entry/exit edge charges nets).
        for (const auto& step : p.steps) {
            auto it = chan_index.find(step.first);
            if (it == chan_index.end()) continue;  // block step
            if (step.second == 1 || step.second == 3) hu[it->second] += p.nets;  // left/right
            else                                      vu[it->second] += p.nets;  // top/bottom
        }
        // (2) Wirelength: dedupe consecutive identical objects -> node sequence, chain guiding pts.
        std::vector<Rect> nodes;
        std::string prev;
        for (const auto& step : p.steps) {
            if (step.first == prev) continue;
            Rect r;
            if (!rect_of(step.first, r)) { prev = step.first; continue; }
            nodes.push_back(r);
            prev = step.first;
        }
        if (nodes.size() < 2) continue;  // direct abutment (1 node after collapse) -> 0 WL
        std::vector<std::pair<double, double>> guides;
        guides.reserve(nodes.size() - 1);
        for (std::size_t i = 0; i + 1 < nodes.size(); ++i) guides.push_back(guidingPoint(nodes[i], nodes[i + 1]));
        double path_len = 0.0;
        for (std::size_t i = 0; i + 1 < guides.size(); ++i)
            path_len += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second);
        wirelength += path_len * p.nets;
    }
    out.wirelength = wirelength;

    // Overflow / capacity: horizontal capacity = 25*height, vertical capacity = 25*width.
    double overflow = 0.0;
    double capacity = 0.0;
    for (std::size_t k = 0; k < cfg.channels.size(); ++k) {
        const Rect& r = cfg.channels[k].rect;
        const double cap_h = kCapPerUm * r.h;
        const double cap_v = kCapPerUm * r.w;
        overflow += std::max(0.0, hu[k] - cap_h) + std::max(0.0, vu[k] - cap_v);
        capacity += cap_h + cap_v;
        out.demand_by_channel[cfg.channels[k].name] = {hu[k], vu[k]};
    }
    out.overflow_nets = overflow;
    out.capacity_nets = capacity;
    out.rate = (capacity > 0.0) ? overflow / capacity : 0.0;

    // Overflow penalty (H1 global piecewise-linear). Base 20*A*R reproduces cases 0/2/3/4 exactly;
    // the beta_tail tail is the best-proven form -- case1 (R>threshold) is ~0.31% of total cost
    // short (no per-channel / direction-split hypothesis reproduced it; beta-task-2-report.md S4).
    const double A = out.outline_area;
    const double R = out.rate;
    out.overflow_penalty = kBetaLin * A * R + kBetaTail * A * std::max(0.0, R - kThreshold);
    out.ft_penalty = 0.0;  // alpha package used no feedthrough

    out.checker_cost = A + problem.alpha * out.wirelength + out.overflow_penalty;
    return out;
}

}  // namespace efgr
