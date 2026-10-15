#pragma once

// A bounded system frame, two rows of charts, then the shared process table in ui.hpp.
static std::vector<std::string> overview(const Snapshot& s, State& state, const Options& o,
                                         int width, int budget, bool color) {
    using Panel = std::vector<std::string>;
    const std::string reset = color ? "\033[0m" : "", dim = color ? "\033[2m" : "";
    const std::string cyan = color ? "\033[36m" : "", green = color ? "\033[32m" : "";
    const std::string yellow = color ? "\033[33m" : "", red = color ? "\033[31m" : "";
    const std::string bold = color ? "\033[1m" : "", edge = dim + cyan;
    const std::string hz = o.ascii ? "-" : "─", vt = o.ascii ? "|" : "│";
    const int inner = width - 4;
    const bool wide = inner >= 112;
    struct Card {
        std::string title, detail;
        History* history;
        double ceiling;
        std::string unit;
        bool load = false, combinedMemory = false;
    };
    std::vector<Card> primary, secondary, temperatures;
    auto gpu = [&]() -> Card {
        if (s.gpuPercent) return {"GPU " + number(s.gpuPercent, 0) + "%", number(s.gpuMHz, 0) + " MHz", &state.gpuHistory, 100, "%", true};
        auto peak = state.gpuMemoryHistory.summary().second;
        return {"GPU MEMORY", memory(s.gpuMemory), &state.gpuMemoryHistory,
            peak ? std::max(64.0, std::ceil(*peak / 64) * 64) : 64, " MiB"};
    };
    auto swap = s.swapTotal && *s.swapTotal == 0 ? Value(0) : percent(s.swapUsed, s.swapTotal);
    if (wide && !s.groups.empty() && s.groups.size() <= 3) {
        for (const auto& group : s.groups) {
            std::string label = group.id;
            bool contiguous = !group.cores.empty();
            for (size_t i = 1; i < group.cores.size(); ++i) contiguous &= group.cores[i] == group.cores[i - 1] + 1;
            if (contiguous) label = std::to_string(group.cores.front()) +
                (group.cores.size() > 1 ? "-" + std::to_string(group.cores.back()) : "");
            primary.push_back({"CPU " + label + "  " + number(group.active, 0) + "%",
                number(group.mhz, 0) + " MHz" + (group.frequencyKind == "requested" ? " req" : "") +
                " | " + std::to_string(group.cores.size()) + " cores", &state.groupHistory[group.id], 100, "%", true});
        }
        if (s.groups.size() < 3) primary.push_back(gpu());
        else secondary.push_back(gpu());
        primary.push_back({"RAM / SWAP", "", &state.memoryHistory, 100, "%", true, true});
    } else {
        primary.push_back({"CPU  " + number(s.cpu, 0) + "%", std::to_string(s.cores.size()) +
            (s.coreSource == "proc-stat" ? " logical cores" : " allowed cores") +
            (s.cpuSource == "hidumper-cpu-service" ? " | service" : ""), &state.cpuHistory, 100, "%", true});
        primary.push_back({"RAM  " + number(percent(s.used, s.total), 0) + "%", memoryPair(s.used, s.total), &state.memoryHistory, 100, "%", true});
        primary.push_back(gpu());
        primary.push_back({"SWAP  " + number(swap, 0) + "%", memoryPair(s.swapUsed, s.swapTotal), &state.swapHistory, 100, "%", true});
    }
    // Keep the two temperature histories under one shared heading, including through transient failures.
    for (const std::string name : {"ddr_type", "shell_back"}) {
        auto sensor = std::find_if(s.sensors.begin(), s.sensors.end(), [&](const auto& item) {
            return item.name == name && item.kind == "temperature";
        });
        std::string path = sensor == s.sensors.end() ? "ThermalService/" + name : sensor->path;
        auto history = state.sensorHistory.find(path);
        if ((sensor == s.sensors.end() || !sensor->value) &&
            (history == state.sensorHistory.end() || !history->second.summary().second)) continue;
        temperatures.push_back({name, number(sensor == s.sensors.end() ? Value{} : sensor->value) + " C",
            &state.sensorHistory[path], 100, " C"});
    }
    const int cols = wide ? static_cast<int>(primary.size()) : 2;
    const int rows = static_cast<int>((primary.size() + cols - 1) / cols);
    const int reserve = state.showProcesses && budget >= 24 ? std::clamp(budget - 17, 11, 20) : 0;
    const int available = reserve ? std::min(17, budget - reserve) : budget;
    int topGraph = state.graphs ? (reserve ? 4 : 8) : 0;
    int bottomGraph = topGraph;
    auto hasSecondary = [&]() { return !secondary.empty() || !temperatures.empty(); };
    auto used = [&]() { return 2 + rows * (topGraph + 4) + rows - 1 +
        (hasSecondary() ? bottomGraph + 5 : 0); };
    const bool combined = std::any_of(primary.begin(), primary.end(), [](const auto& c) { return c.combinedMemory; });
    const int minimum = state.graphs ? (combined ? 2 : 1) : 0;
    while (used() > available) {
        if (hasSecondary() && bottomGraph > minimum && bottomGraph >= topGraph) --bottomGraph;
        else if (topGraph > minimum) --topGraph;
        else if (hasSecondary() && bottomGraph > minimum) --bottomGraph;
        else if (hasSecondary()) { secondary.clear(); temperatures.clear(); }
        else break;
    }
    // A short window may omit the secondary row; return that space to the remaining charts.
    const int preferred = state.graphs ? (reserve ? 4 : 8) : 0;
    while (topGraph < preferred && used() + rows <= available) ++topGraph;
    auto graph = [&](const History& history, int w, int h, double ceiling, bool load) {
        Panel result = history.graph(w, h, ceiling, o.ascii);
        if (!color) return result;
        const auto values = history.columns(w);
        for (auto& row : result) {
            std::string painted;
            size_t offset = 0;
            for (int x = 0; x < w; ++x) {
                const unsigned char c = row[offset];
                size_t n = c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
                painted += load && values[x] ? (*values[x] >= 85 ? red : *values[x] >= 60 ? yellow : green) : cyan;
                painted += row.substr(offset, n); offset += n;
            }
            row = painted + reset;
        }
        return result;
    };
    auto top = [&](const std::string& title, int w) {
        const std::string label = " " + fit(title, std::min(w - 4, static_cast<int>(title.size()))) + " ";
        return edge + (o.ascii ? "+" : "┌") + bold + cyan + label + reset + edge +
            repeatGlyph(hz, w - 2 - static_cast<int>(label.size())) + (o.ascii ? "+" : "┐") + reset;
    };
    auto card = [&](const Card& c, int w, int h) {
        Panel body, result{top(c.title, w)};
        if (c.combinedMemory) {
            auto memoryLabel = [&](const std::string& name, Value value, Value total, Value ratio) {
                std::string text = name + " " + memoryPair(value, total);
                std::string usage = " " + number(ratio, 0) + "%";
                if (text.size() + usage.size() <= static_cast<size_t>(w - 4)) text += usage;
                return bold + text + reset;
            };
            body.push_back(memoryLabel("RAM", s.used, s.total, percent(s.used, s.total)));
            for (const auto& line : graph(state.memoryHistory, w - 4, (h + 1) / 2, 100, true)) body.push_back(line);
            body.push_back(memoryLabel("SWAP", s.swapUsed, s.swapTotal, swap));
            for (const auto& line : graph(state.swapHistory, w - 4, h / 2, 100, true)) body.push_back(line);
        } else {
            body.push_back(bold + c.detail + reset);
            for (const auto& line : graph(*c.history, w - 4, h, c.ceiling, c.load)) body.push_back(line);
            const auto stats = c.history->summary();
            body.push_back(dim + "avg " + number(stats.first) + "  max " + number(stats.second) + c.unit + reset);
        }
        for (const auto& line : body) result.push_back(edge + vt + reset + " " + cellFit(line, w - 4) + reset + " " + edge + vt + reset);
        result.push_back(edge + (o.ascii ? "+" : "└") + repeatGlyph(hz, w - 2) + (o.ascii ? "+" : "┘") + reset);
        return result;
    };
    auto temperaturePanel = [&](int w, int h) {
        Panel result{top("TEMPERATURE", w)};
        const int count = static_cast<int>(temperatures.size());
        const int plotSpace = w - 4 - 3 * (count - 1);
        std::vector<Panel> plots;
        std::vector<int> widths;
        for (int i = 0; i < count; ++i) {
            const Card& c = temperatures[i];
            const int plotWidth = plotSpace / count + (i < plotSpace % count ? 1 : 0);
            widths.push_back(plotWidth);
            Panel plot{cyan + c.title + reset + "  " + bold + c.detail + reset};
            for (const auto& line : graph(*c.history, plotWidth, h, 100, false)) plot.push_back(line);
            const auto stats = c.history->summary();
            plot.push_back(dim + "avg " + number(stats.first) + "  max " + number(stats.second) + " C" + reset);
            plots.push_back(std::move(plot));
        }
        for (int y = 0; y < h + 2; ++y) {
            std::string line = edge + vt + reset + " ";
            for (int i = 0; i < count; ++i) {
                if (i) line += " " + edge + vt + reset + " ";
                line += cellFit(plots[i][y], widths[i]) + reset;
            }
            result.push_back(line + " " + edge + vt + reset);
        }
        result.push_back(edge + (o.ascii ? "+" : "└") + repeatGlyph(hz, w - 2) + (o.ascii ? "+" : "┘") + reset);
        return result;
    };
    Panel content;
    auto row = [&](const std::vector<Card>& cards, size_t start, int count, int h) {
        std::vector<Panel> panels;
        for (int i = 0; i < count; ++i) {
            int w = (inner - count + 1) / count + (i < (inner - count + 1) % count ? 1 : 0);
            panels.push_back(start + i < cards.size() ? card(cards[start + i], w, h) : Panel(h + 4, std::string(w, ' ')));
        }
        for (int y = 0; y < h + 4; ++y) {
            std::string line;
            for (int i = 0; i < count; ++i) { if (i) line += ' '; line += panels[i][y]; }
            content.push_back(line);
        }
    };
    for (size_t i = 0; i < primary.size(); i += cols) { if (i) content.push_back(std::string(inner, ' ')); row(primary, i, cols, topGraph); }
    if (hasSecondary()) {
        content.push_back(std::string(inner, ' '));
        if (temperatures.empty()) row(secondary, 0, static_cast<int>(secondary.size()), bottomGraph);
        else if (secondary.empty()) {
            auto panel = temperaturePanel(inner, bottomGraph);
            content.insert(content.end(), panel.begin(), panel.end());
        } else {
            const int gpuWidth = inner / 3;
            const auto left = card(secondary.front(), gpuWidth, bottomGraph);
            const auto right = temperaturePanel(inner - gpuWidth - 1, bottomGraph);
            for (size_t i = 0; i < left.size(); ++i) content.push_back(left[i] + " " + right[i]);
        }
    }
    Panel result{top("SYSTEM  " + std::to_string(s.cores.size()) + " cores | " + memory(s.total) + " RAM", width)};
    for (const auto& line : content) result.push_back(edge + vt + reset + " " + line + " " + edge + vt + reset);
    result.push_back(edge + (o.ascii ? "+" : "└") + repeatGlyph(hz, width - 2) + (o.ascii ? "+" : "┘") + reset);
    return result;
}
