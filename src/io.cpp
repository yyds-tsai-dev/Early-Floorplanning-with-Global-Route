#include "io.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "eval.hpp"

namespace efgr {
namespace {

std::vector<std::vector<std::string>> readCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open input CSV: " + path);

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quote = false;
    char c = '\0';
    while (in.get(c)) {
        if (c == '"') {
            if (in_quote && in.peek() == '"') {
                field.push_back('"');
                in.get(c);
            } else {
                in_quote = !in_quote;
            }
            } else if (c == ',' && !in_quote) {
                if (rows.empty() && row.empty() && field.size() >= 3 &&
                    static_cast<unsigned char>(field[0]) == 0xEF &&
                    static_cast<unsigned char>(field[1]) == 0xBB &&
                    static_cast<unsigned char>(field[2]) == 0xBF) {
                    field.erase(0, 3);
                }
                row.push_back(trim(field));
                field.clear();
            } else if ((c == '\n' || c == '\r') && !in_quote) {
                if (c == '\r' && in.peek() == '\n') in.get(c);
                if (rows.empty() && row.empty() && field.size() >= 3 &&
                    static_cast<unsigned char>(field[0]) == 0xEF &&
                    static_cast<unsigned char>(field[1]) == 0xBB &&
                    static_cast<unsigned char>(field[2]) == 0xBF) {
                    field.erase(0, 3);
                }
                row.push_back(trim(field));
                field.clear();
                rows.push_back(row);
            row.clear();
        } else {
            field.push_back(c);
        }
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(trim(field));
        rows.push_back(row);
    }
    return rows;
}

double parseDouble(const std::string& s, double fallback = 0.0) {
    std::string t = trim(s);
    if (t.empty()) return fallback;
    if (!t.empty() && t.back() == '%') t.pop_back();
    try {
        return std::stod(t);
    } catch (...) {
        return fallback;
    }
}

bool hasPercent(const std::string& s) {
    return s.find('%') != std::string::npos;
}

std::vector<std::string> splitLocations(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::vector<int> splitEdges(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        int edge = static_cast<int>(std::llround(parseDouble(item, 0.0)));
        if (edge >= 1 && edge <= 4) out.push_back(edge);
    }
    return out;
}

void parseAspect(Block& b, const std::string& value) {
    std::string v = trim(value);
    auto pos = v.find(',');
    if (pos == std::string::npos) {
        double ar = parseDouble(v, 1.0);
        b.ar_min = ar;
        b.ar_max = ar;
    } else {
        b.ar_min = parseDouble(v.substr(0, pos), 1.0);
        b.ar_max = parseDouble(v.substr(pos + 1), b.ar_min);
    }
    if (b.ar_min <= 0.0) b.ar_min = 1.0;
    if (b.ar_max < b.ar_min) std::swap(b.ar_min, b.ar_max);
}

BlockType parseType(const std::string& s) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::toupper(c); });
    if (t.find("EDGE") != std::string::npos) return BlockType::Edge;
    if (t.find("MACRO") != std::string::npos || t.find("HARD") != std::string::npos) return BlockType::Macro;
    return BlockType::Soft;
}

}  // namespace

Problem parseProblem(const std::string& path) {
    auto rows = readCsv(path);
    Problem p;

    bool in_block_table = false;
    for (const auto& row : rows) {
        if (row.empty()) continue;
        if (row[0] == "BLOCK") {
            in_block_table = true;
            continue;
        }
        if (row[0] == "OUTLINE" || row[0] == "CONN MATRIX") {
            in_block_table = false;
        }
        if (in_block_table && row[0].rfind("BLK", 0) == 0) {
            Block b;
            b.name = row[0];
            b.area = row.size() > 1 ? parseDouble(row[1]) : 0.0;
            b.ft_base_area = b.area;
            b.width = row.size() > 2 ? parseDouble(row[2]) : 0.0;
            b.height = row.size() > 3 ? parseDouble(row[3]) : 0.0;
            parseAspect(b, row.size() > 4 ? row[4] : "1");
            b.type = parseType(row.size() > 5 ? row[5] : "SOFT");
            b.locations = splitLocations(row.size() > 6 ? row[6] : "");
            std::size_t ft_start = 7;
            if (row.size() > 7 && !row[7].empty() && !hasPercent(row[7])) {
                b.port_edges = splitEdges(row[7]);
                b.port_edge = b.port_edges.empty() ? 0 : b.port_edges.front();
                ft_start = 8;
            }
            for (std::size_t i = ft_start; i < row.size() && b.ft_rates.size() < 4; ++i) {
                if (row[i].empty()) continue;
                b.ft_rates.push_back(parseDouble(row[i]) / 100.0);
            }
            while (b.ft_rates.size() < 4) b.ft_rates.push_back(0.2);
            p.block_index[b.name] = static_cast<int>(p.blocks.size());
            p.blocks.push_back(b);
        }
    }

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (row.empty()) continue;
        if (row[0] == "MAX" && row.size() >= 3) {
            p.outline_w = parseDouble(row[1]);
            p.outline_h = parseDouble(row[2]);
        }
        if (!row.empty() && row[0] == "α" && row.size() >= 2) {
            p.alpha = parseDouble(row[1], 1.0);
        }
        if (!row.empty() && row[0].find("ON CHIP") != std::string::npos && i + 1 < rows.size()) {
            auto header = rows[i + 1];
            for (std::size_t r = i + 2; r < rows.size(); ++r) {
                if (rows[r].empty() || rows[r][0].rfind("BLK", 0) != 0) break;
                auto src_it = p.block_index.find(rows[r][0]);
                if (src_it == p.block_index.end()) continue;
                for (std::size_t c = 1; c < rows[r].size() && c < header.size(); ++c) {
                    auto dst_name = trim(header[c]);
                    auto dst_it = p.block_index.find(dst_name);
                    int nets = static_cast<int>(std::llround(parseDouble(rows[r][c])));
                    if (dst_it != p.block_index.end() && nets > 0) {
                        if (src_it->second == dst_it->second) continue;  // QA A44: diagonal entries are data errata
                        p.bundles.push_back(Bundle{src_it->second, dst_it->second, nets});
                    }
                }
            }
        }
    }

    if (p.blocks.empty()) throw std::runtime_error("no blocks parsed from " + path);
    if (p.outline_w <= 0.0 || p.outline_h <= 0.0) throw std::runtime_error("outline MAX missing or invalid");
    return p;
}

// L6 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md §L6.1): unordered-pair
// key (min,max), nets summed over both directed entries, output sorted by (source,target) ascending
// via the std::map's own iteration order. Idempotent: every emitted Bundle already has source<target
// and a unique key, so re-merging an already-merged vector reproduces it unchanged.
std::vector<Bundle> mergeUndirectedBundles(const std::vector<Bundle>& bundles) {
    std::map<std::pair<int, int>, int> merged;
    for (const auto& b : bundles) {
        int lo = std::min(b.source, b.target);
        int hi = std::max(b.source, b.target);
        merged[{lo, hi}] += b.nets;
    }
    std::vector<Bundle> out;
    out.reserve(merged.size());
    for (const auto& [key, nets] : merged) {
        out.push_back(Bundle{key.first, key.second, nets});
    }
    return out;
}

void writeCfg(const std::string& path, const Problem& problem, const Placement& placement,
              const RoutingResult& routing) {
    // B2 (anytime output, .superpowers/sdd/b2-brief.md item 2): write to a temp file next to `path`,
    // then atomically rename over it -- a reader (or a kill mid-write) never observes a partially
    // written cfg. std::rename is atomic when source and destination share a filesystem, true for
    // every caller here (`path + ".tmp"` always sits next to `path`). This makes EVERY writeCfg call
    // anytime-safe -- runPricingPortfolio's optional on_improved callback (search.hpp) may call this
    // many times against the SAME output path over one run -- with no separate "unsafe" writer to
    // keep in sync: main.cpp's own final write uses this exact same function.
    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream out(tmp_path);
        if (!out) throw std::runtime_error("cannot open output cfg: " + tmp_path);
        out << std::fixed << std::setprecision(3);
        // Emit the placement's declared (shrink-wrapped) outline when stamped -- design
        // p1-design.md §1.6 -- falling back to the MAX-clamped usedOutline bbox when unset (0),
        // which reproduces today's behavior for placements that never shrink-wrapped.
        double out_w = placement.outline_w;
        double out_h = placement.outline_h;
        if (out_w <= 0.0 || out_h <= 0.0) {
            auto [used_w, used_h] = usedOutline(problem, placement, routing);
            out_w = used_w;
            out_h = used_h;
        }
        out << "OUTLINE " << out_w << " " << out_h << "\nEND\n";
        out << "BLOCK\n";
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            const auto& r = placement.rects[i];
            out << problem.blocks[i].name << " " << r.x << " " << r.y << " " << r.w << " " << r.h << "\n";
        }
        out << "END\n";
        out << "CHANNEL\n";
        for (const auto& ch : routing.channels) {
            out << ch.name << " " << ch.rect.x << " " << ch.rect.y << " " << ch.rect.w << " " << ch.rect.h << "\n";
        }
        out << "END\n";
        out << "PATH\n";
        for (const auto& route : routing.routes) {
            if (route.open || route.steps.empty()) continue;
            out << "PATH " << route.nets;
            for (const auto& step : route.steps) out << " " << step.object << " " << step.edge;
            out << "\n";
        }
        out << "END\n";
    }  // `out` flushes and closes here (RAII scope exit) before the rename below.
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        throw std::runtime_error("cannot atomically publish output cfg: " + tmp_path + " -> " + path);
    }
}

}  // namespace efgr
