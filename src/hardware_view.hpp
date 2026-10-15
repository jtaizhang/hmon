#pragma once

struct TemperatureName { std::string label; int order; };
static TemperatureName temperatureName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    // General shell/battery labels follow SmartPerf terminology. Asterisks are name-based
    // interpretations, NOT verified physical probe locations on a particular PC model.
    if (lower == "ddr_type") return {"内存 DDR 区域*", 0};
    if (lower == "shell_back") return {"后壳", 1};
    if (lower == "shell_front") return {"前壳", 2};
    if (lower == "shell_frame") return {"机壳边框", 3};
    if (lower == "battery") return {"电池", 4};
    if (lower == "ambient") return {"环境测点*", 5};
    if (lower == "type_c") return {"USB-C 区域*", 6};
    if (lower == "rfboard") return {"射频板区域*", 7};
    if (lower == "drmos_cpub") return {"CPU 供电区域*", 8};
    if (lower == "drmos_gpu_npu") return {"GPU/NPU 供电区域*", 9};
    if (lower == "system_h") return {"系统测点 / 位置未明", 10};
    if (lower == "shell") return {"机壳 / 位置未明", 11};
    return {"测点 / 位置未确认", 12};
}

static std::vector<std::string> hardwareView(const Snapshot& s, State& state, const Options& o,
        int width, int budget, bool color, std::string& context) {
    using Panel = std::vector<std::string>;
    struct Plot {
        std::string title, subtitle, unit;
        Value value;
        const History* history;
    };
    const std::string reset = color ? "\033[0m" : "", dim = color ? "\033[2m" : "";
    const std::string cyan = color ? "\033[36m" : "", green = color ? "\033[32m" : "";
    const std::string bold = color ? "\033[1m" : "", edge = dim + cyan;
    const std::string hz = o.ascii ? "-" : "─", vt = o.ascii ? "|" : "│";
    std::vector<Plot> frequencies, temperatures;
    double frequencyCeiling = 1000;
    for (const auto& group : s.groups) {
        std::string cores;
        bool contiguous = !group.cores.empty();
        for (size_t i = 1; i < group.cores.size(); ++i) contiguous &= group.cores[i] == group.cores[i - 1] + 1;
        if (contiguous) cores = std::to_string(group.cores.front()) +
            (group.cores.size() > 1 ? "-" + std::to_string(group.cores.back()) : "");
        else for (int core : group.cores) { if (!cores.empty()) cores += ','; cores += std::to_string(core); }
        const auto& history = state.groupFrequencyHistory[group.id];
        auto peak = history.summary().second;
        if (peak) frequencyCeiling = std::max(frequencyCeiling, std::ceil(*peak / 500) * 500);
        frequencies.push_back({"CPU 核心 " + (cores.empty() ? "--" : cores),
            std::string(group.frequencyKind == "requested" ? "请求频率" : "实测频率") + " | " +
            std::to_string(group.cores.size()) + " 核 | " + group.id, "MHz", group.mhz, &history});
    }
    if (s.gpuMHz || state.gpuFrequencyHistory.summary().second) {
        auto peak = state.gpuFrequencyHistory.summary().second;
        if (peak) frequencyCeiling = std::max(frequencyCeiling, std::ceil(*peak / 500) * 500);
        frequencies.push_back({"GPU", "GPU 频率", "MHz", s.gpuMHz, &state.gpuFrequencyHistory});
    }
    std::vector<const Sensor*> sensors;
    for (const auto& sensor : s.sensors) if (sensor.kind == "temperature" &&
        (sensor.value || state.sensorHistory[sensor.path].summary().second)) sensors.push_back(&sensor);
    std::stable_sort(sensors.begin(), sensors.end(), [](const auto* a, const auto* b) {
        return temperatureName(a->name).order < temperatureName(b->name).order;
    });
    for (const auto* sensor : sensors) temperatures.push_back({temperatureName(sensor->name).label,
        sensor->name, sensor->unit, sensor->value, &state.sensorHistory[sensor->path]});

    const int columns = width >= 118 ? 3 : 2;
    const int frequencyRows = std::max(1, static_cast<int>((frequencies.size() + columns - 1) / columns));
    const int temperatureRows = std::max(1, static_cast<int>((temperatures.size() + columns - 1) / columns));
    const int minimumGraph = state.graphs ? 1 : 0;
    const int totalRows = frequencyRows + temperatureRows;
    const int minimumHeight = 3 + totalRows * (minimumGraph + 4);
    const int reserve = state.showProcesses && minimumHeight + 9 <= budget ? 9 : 0;
    const int available = budget - reserve;
    const int graphHeight = state.graphs ? std::clamp((available - 3) / totalRows - 4, 1, 4) : 0;
    const int slots = std::max(2, (available - 3) / (graphHeight + 4));
    int frequencyPerPage = std::min(frequencyRows, std::max(1, slots / 2));
    const int temperaturePerPage = std::min(temperatureRows, slots - frequencyPerPage);
    frequencyPerPage = std::min(frequencyRows, slots - temperaturePerPage);
    const size_t frequencyPages = (frequencyRows + frequencyPerPage - 1) / frequencyPerPage;
    const size_t temperaturePages = (temperatureRows + temperaturePerPage - 1) / temperaturePerPage;
    state.pageCount = std::max(frequencyPages, temperaturePages);
    state.page = std::min(state.page, state.pageCount - 1);

    auto section = [&](const std::string& name, const std::vector<Plot>& plots, int perPage,
                       size_t pageCount, double ceiling, const std::string& ink, const std::string& empty) {
        const size_t page = std::min(state.page, pageCount - 1);
        const size_t start = page * perPage * columns, end = std::min(plots.size(), start + perPage * columns);
        std::string title = name + (pageCount > 1 ? "  " + std::to_string(page + 1) + "/" + std::to_string(pageCount) : "");
        const std::string label = " " + cellFit(title, std::min(width - 4, terminalCells(title))) + " ";
        Panel result{edge + (o.ascii ? "+" : "┌") + bold + cyan + label + reset + edge +
            repeatGlyph(hz, width - 2 - terminalCells(label)) + (o.ascii ? "+" : "┐") + reset};
        auto body = [&](const std::string& text) {
            result.push_back(edge + vt + reset + " " + cellFit(text, width - 4) + reset + " " + edge + vt + reset);
        };
        if (plots.empty()) body(dim + empty + reset);
        for (size_t row = start; row < end; row += columns) {
            if (row > start) body(edge + repeatGlyph(hz, width - 4) + reset);
            const int space = width - 4 - 3 * (columns - 1);
            std::vector<Panel> children;
            for (int col = 0; col < columns; ++col) {
                const int w = space / columns + (col < space % columns ? 1 : 0);
                if (row + col >= end) { children.push_back(Panel(graphHeight + 3, std::string(w, ' '))); continue; }
                const Plot& plot = plots[row + col];
                const std::string value = number(plot.value, plot.unit == "MHz" ? 0 : 1) + " " + plot.unit;
                Panel child{ink + cellFit(plot.title, std::max(0, w - terminalCells(value) - 1)) + " " + bold + value + reset,
                    dim + cellFit(plot.subtitle, w) + reset};
                for (const auto& line : plot.history->graph(w, graphHeight, ceiling, o.ascii)) child.push_back(ink + line + reset);
                auto stats = plot.history->summary();
                child.push_back(dim + cellFit("均值 " + number(stats.first, plot.unit == "MHz" ? 0 : 1) +
                    "  峰值 " + number(stats.second, plot.unit == "MHz" ? 0 : 1) + " " + plot.unit, w) + reset);
                children.push_back(std::move(child));
            }
            for (int y = 0; y < graphHeight + 3; ++y) {
                std::string line;
                for (int col = 0; col < columns; ++col) {
                    if (col) line += " " + edge + vt + reset + " ";
                    line += children[col][y];
                }
                body(line);
            }
        }
        result.push_back(edge + (o.ascii ? "+" : "└") + repeatGlyph(hz, width - 2) + (o.ascii ? "+" : "┘") + reset);
        return result;
    };
    Panel result = section("频率 FREQUENCY | 0-" + number(frequencyCeiling, 0) + " MHz", frequencies,
        frequencyPerPage, frequencyPages, frequencyCeiling, cyan, "暂无可读取的频率");
    result.push_back(std::string(width, ' '));
    auto thermal = section("温度 TEMPERATURE | 0-100 C", temperatures, temperaturePerPage,
        temperaturePages, 100, green, "暂无可读取的温度");
    result.insert(result.end(), thermal.begin(), thermal.end());
    context = state.pageCount > 1 ? "左右键: 频率/温度翻页 " + std::to_string(state.page + 1) + "/" +
        std::to_string(state.pageCount) + " | " : "";
    context += "* 区域按名称推断；机壳/供电温度不是核心温度";
    return result;
}
