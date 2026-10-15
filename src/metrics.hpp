#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <limits>

namespace hmon {
using Value = std::optional<double>;
struct CpuTicks { std::array<uint64_t, 8> ticks{}; };
using CpuMap = std::map<std::string, CpuTicks>;
inline CpuMap parseCpu(const std::string& text) {
    CpuMap result;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        std::istringstream fields(line);
        std::string name;
        fields >> name;
        if (name != "cpu" && (name.size() <= 3 || name.substr(0, 3) != "cpu" ||
            name.find_first_not_of("0123456789", 3) != std::string::npos)) continue;
        CpuTicks value;
        int count = 0;
        bool invalid = false;
        for (auto& tick : value.ticks) {
            std::string token;
            if (!(fields >> token)) break;
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos) { invalid = true; break; }
            try { tick = std::stoull(token); ++count; } catch (...) { invalid = true; break; }
        }
        if (count >= 4 && !invalid) result[name] = value;
    }
    return result;
}
inline Value cpuDelta(const CpuTicks& before, const CpuTicks& after) {
    double total = 0, idle = 0;
    for (size_t i = 0; i < before.ticks.size(); ++i) {
        // Counter reset, hotplug, or iowait rollback: establish a new baseline.
        if (after.ticks[i] < before.ticks[i]) return {};
        double delta = static_cast<double>(after.ticks[i] - before.ticks[i]);
        total += delta;
        if (i == 3 || i == 4) idle += delta;
    }
    if (total <= 0) return {};
    return std::clamp(100.0 * (total - idle) / total, 0.0, 100.0);
}
inline Value kbField(const std::string& text, const std::string& key) {
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        std::istringstream fields(line);
        std::string name, token, unit, extra;
        if (!(fields >> name >> token >> unit) || name != key + ":" || unit != "kB") continue;
        if ((fields >> extra) || token.find_first_not_of("0123456789") != std::string::npos) return {};
        try { return static_cast<double>(std::stoull(token)); } catch (...) { return {}; }
    }
    return {};
}
inline Value difference(Value total, Value free) {
    if (!total || !free || *free > *total) return {};
    return *total - *free;
}
inline std::string safeName(const std::string& input) {
    // ASCII-only process labels prevent terminal escapes, bidi controls and width ambiguity.
    std::string output;
    for (unsigned char c : input) {
        if (c == 0 || output.size() == 256) break;
        output += (c >= 32 && c <= 126) ? static_cast<char>(c) : '?';
    }
    return output;
}
inline std::optional<uint64_t> unsignedInteger(const std::string& token) {
    if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos) return {};
    try { return std::stoull(token); } catch (...) { return {}; }
}
inline Value scalar(const std::string& text) {
    std::istringstream in(text);
    double value; std::string extra;
    if (!(in >> value) || (in >> extra) || !std::isfinite(value)) return {};
    return value;
}
inline Value cpuServicePercent(const std::string& text) {
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        if (line.substr(0, 7) != "Total: ") continue;
        auto end = line.find('%', 7);
        if (end == std::string::npos) continue;
        Value value = scalar(line.substr(7, end - 7));
        if (value && *value >= 0 && *value <= 100) return value;
    }
    return {};
}
struct ProcessTicks { uint64_t user = 0, system = 0, start = 0; };
inline std::optional<ProcessTicks> parseProcessTicks(const std::string& text) {
    // comm (field 2) can contain spaces and parentheses. All fields after its final ')' are numeric/state.
    auto first = text.find('('), last = text.rfind(')');
    if (first == std::string::npos || last == std::string::npos || first >= last) return {};
    std::istringstream in(text.substr(last + 1));
    std::vector<std::string> fields;
    for (std::string token; in >> token;) fields.push_back(token);
    if (fields.size() < 20 || fields[0].size() != 1) return {};
    auto user = unsignedInteger(fields[11]), system = unsignedInteger(fields[12]), start = unsignedInteger(fields[19]);
    if (!user || !system || !start) return {};
    return ProcessTicks{*user, *system, *start};
}
inline Value processCpu(const ProcessTicks& before, const ProcessTicks& after, double seconds, long hz) {
    if (hz <= 0 || seconds <= 0 || !std::isfinite(seconds) || before.start != after.start ||
        after.user < before.user || after.system < before.system) return {};
    // 100% = one logical CPU. Multithreaded processes can exceed 100%; no per-process clamp.
    return (static_cast<double>(after.user - before.user) + static_cast<double>(after.system - before.system)) / hz / seconds * 100;
}
inline std::vector<int> cpuIds(std::string text) {
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream in(text); std::vector<int> result;
    for (std::string token; in >> token;) {
        auto dash = token.find('-');
        auto a = unsignedInteger(token.substr(0, dash));
        auto b = dash == std::string::npos ? a : unsignedInteger(token.substr(dash + 1));
        if (!a || !b || *a > *b || *b > 4095) return {};
        for (uint64_t n = *a; n <= *b; ++n) result.push_back(static_cast<int>(n));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
struct Process { int pid = 0; std::string name; Value rss, pss, cpu, uid; };
enum class Sort { Rss, Pss, Cpu };
inline bool processLess(const Process& a, const Process& b, Sort sort) {
    const Value& left = sort == Sort::Cpu ? a.cpu : sort == Sort::Pss ? a.pss : a.rss;
    const Value& right = sort == Sort::Cpu ? b.cpu : sort == Sort::Pss ? b.pss : b.rss;
    if (left.has_value() != right.has_value()) return left.has_value();
    if (left && *left != *right) return *left > *right;
    return a.pid < b.pid;
}
inline std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
inline bool matches(const Process& p, const std::string& query) {
    return lower(p.name).find(lower(query)) != std::string::npos ||
           std::to_string(p.pid).find(query) != std::string::npos;
}
} // namespace hmon
