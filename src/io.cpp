#include "io.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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
            row.push_back(trim(field));
            field.clear();
        } else if ((c == '\n' || c == '\r') && !in_quote) {
            if (c == '\r' && in.peek() == '\n') in.get(c);
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
            b.width = row.size() > 2 ? parseDouble(row[2]) : 0.0;
            b.height = row.size() > 3 ? parseDouble(row[3]) : 0.0;
            parseAspect(b, row.size() > 4 ? row[4] : "1");
            b.type = parseType(row.size() > 5 ? row[5] : "SOFT");
            b.locations = splitLocations(row.size() > 6 ? row[6] : "");
            std::size_t ft_start = 7;
            if (row.size() > 7 && !row[7].empty() && !hasPercent(row[7])) {
                b.port_edge = static_cast<int>(std::llround(parseDouble(row[7], 0.0)));
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

void writeCfg(const std::string& path, const Problem& problem, const Placement& placement,
              const RoutingResult& routing) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot open output cfg: " + path);
    out << std::fixed << std::setprecision(3);
    out << "OUTLINE " << problem.outline_w << " " << problem.outline_h << "\nEND\n";
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
}

}  // namespace efgr
