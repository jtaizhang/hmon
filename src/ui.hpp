#pragma once
// Pure rendering. CJK labels occupy two terminal cells; chart glyphs occupy one.
static int glyphCells(const std::string& text, size_t at, size_t length) {
    unsigned char first = text[at];
    unsigned int cp = length == 1 ? first : first & (0x7F >> length);
    for (size_t j = 1; j < length && at + j < text.size(); ++j)
        cp = (cp << 6) | (static_cast<unsigned char>(text[at + j]) & 0x3F);
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE6F) || (cp >= 0xFF01 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ? 2 : 1;
}
static int terminalCells(const std::string& text) {
    int cells = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            auto end = text.find('m', i + 2);
            if (end != std::string::npos) { i = end + 1; continue; }
        }
        unsigned char c = text[i];
        size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        cells += glyphCells(text, i, n); i += n;
    }
    return cells;
}
static std::string cellFit(const std::string& text, int width) {
    std::string result; int cells = 0;
    for (size_t i = 0; i < text.size() && cells < width;) {
        // Only renderer-owned SGR sequences enter here; external labels are sanitized before rendering.
        if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            auto end = text.find('m', i + 2);
            if (end != std::string::npos) { result += text.substr(i, end - i + 1); i = end + 1; continue; }
        }
        unsigned char c = text[i];
        size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        int size = glyphCells(text, i, n);
        if (cells + size > width) break;
        result += text.substr(i, n); i += n; cells += size;
    }
    return result + std::string(std::max(0, width - cells), ' ');
}
static std::string repeatGlyph(const std::string& glyph, int count) {
    std::string result; while (count-- > 0) result += glyph; return result;
}
#include "overview.hpp"
#include "hardware_view.hpp"
static std::vector<std::string> dashboard(const Snapshot& s, State& state, const Options& o, int columns, int height, bool color) {
    int width = std::clamp(columns - 1, 1, 240); height = std::clamp(height, 4, 120);
    const int limit = height - 3;
    color = color && !o.plain;
    const std::string cyan = color ? "\033[36m" : "", green = color ? "\033[32m" : "", yellow = color ? "\033[33m" : "";
    const std::string muted = color ? "\033[2m" : "", bold = color ? "\033[1m" : "", reset = color ? "\033[0m" : "";
    const std::string hz = o.ascii ? "-" : "─", vt = o.ascii ? "|" : "│";
    using Rows = std::vector<std::pair<std::string, std::string>>;
    using Panel = std::vector<std::string>;
    Panel lines;
    std::string context;
    state.pageCount = 1;
    auto line = [&](const std::string& text, const std::string& ink = "") { lines.push_back(ink + cellFit(text, width) + reset); };
    auto box = [&](const std::string& title, const Rows& rows, int w, const std::string& ink) {
        Panel out; auto label = fit(" " + title + " ", std::min(w - 4, static_cast<int>(title.size()) + 2));
        out.push_back(ink + (o.ascii ? "+" : "┌") + hz + label + repeatGlyph(hz, w - 3 - static_cast<int>(label.size())) + (o.ascii ? "+" : "┐") + reset);
        for (const auto& r : rows) out.push_back(ink + vt + reset + " " + r.second + cellFit(r.first, w - 4) + reset + " " + ink + vt + reset);
        out.push_back(ink + (o.ascii ? "+" : "└") + repeatGlyph(hz, w - 2) + (o.ascii ? "+" : "┘") + reset);
        return out;
    };
    auto append = [&](const Panel& p) { lines.insert(lines.end(), p.begin(), p.end()); };
    auto join = [&](const std::vector<Panel>& panels) {
        if (panels.empty()) return;
        for (size_t r = 0; r < panels.front().size(); ++r) {
            std::string joined;
            for (size_t p = 0; p < panels.size(); ++p) { if (p) joined += ' '; joined += panels[p][r]; }
            lines.push_back(joined);
        }
    };
    auto metric = [&](const std::string& title, const std::string& detail, const History& history, double ceiling,
                      const std::string& unit, int w, int h, const std::string& ink) {
        Rows rows{{detail, bold + ink}};
        if (state.graphs) for (const auto& r : history.graph(w - 4, h, ceiling, o.ascii)) rows.emplace_back(r, ink);
        auto [avg, max] = history.summary();
        rows.emplace_back("avg " + number(avg) + "  max " + number(max) + unit, muted);
        return box(title, rows, w, cyan);
    };
    auto pages = [&](size_t count, size_t perPage, const std::string& name) {
        state.pageCount = std::max<size_t>(1, (count + perPage - 1) / perPage);
        state.page = std::min(state.page, state.pageCount - 1);
        if (state.pageCount > 1) context = name + " " + std::to_string(state.page + 1) + "/" + std::to_string(state.pageCount) + "  Left/Right: previous/next page";
    };
    const std::string view = state.help ? "Help" : state.view == 0 ? "Overview" : state.view == 1 ? "CPU cores" : "Hardware";
    std::string title = " hmon 0.2.7 / " + view + (o.emulator ? " / EMULATOR" : "");
    std::string refresh = (state.paused ? "Paused | " : "") + std::string("Refresh ") + number(o.interval) + "s";
    if (width >= 90) refresh += " | History " + number(state.cpuHistory.span(), 0) + "s";
    int gap = std::max(1, width - static_cast<int>(title.size() + refresh.size()) - 1);
    line(title + std::string(gap, ' ') + refresh, cyan);
    if (width < 60 || height < 18) {
        if (height >= 5) line(" CPU " + number(s.cpu) + "%  RAM " + memoryPair(s.used, s.total), green);
        if (height >= 6) line(" Enlarge to 61 columns x 18 rows.", muted);
    } else if (state.help) {
        const std::vector<std::string> help{
            "q / Ctrl+C  Quit       p / Space  Pause or resume",
            "v  Switch overview, CPU cores and hardware",
            "t  Show or hide processes     b  Show or hide charts",
            "r  Cycle refresh: 0.5 / 1 / 2 / 5 seconds",
            "+ or =  Add 0.5 seconds      - or _  Subtract 0.5s",
            "0 / 1 / 2 / 5  Set interval to 0.5 / 1 / 2 / 5s",
            "Refresh range: 0.5-10 seconds; value shown at top.",
            "s  Cycle sort: Memory (RSS), CPU, Memory (PSS)",
            "   Highest values first. c / m: CPU / RSS shortcuts.",
            "/  Search process name or PID; Enter apply; Esc clear",
            "Up/Down or j/k  Scroll processes       g  Back to top",
            "Left/Right or [ / ]  Previous/next page when needed",
            "? or Esc  Close help", "",
            "RAM and Swap: used / total. 100% CPU = one core.",
            "RSS includes shared memory in each process.",
            "PSS splits shared memory among processes (no swap).",
            "PSS may need more time; -- means permission denied",
            "or unavailable. Only processes visible to HiShell",
            "are listed; this is not a complete system list.",
            "Charts: last 120 samples, oldest left, newest right.",
            "avg / max: average / peak within retained history.",
            "Missing samples leave gaps. GPU memory is not load.",
            "Hardware groups follow CPU clock policies, not E/P.",
            "Requested MHz are marked req. Sensor labels come",
            "from drivers; no guessed CPU/GPU/NPU power values.",
            "Temperature service values keep system sensor names.",
            "Board/case readings are not CPU/GPU die temperatures.",
            "Unknown fan values are not shown as RPM.",
            "Run --diagnose for source paths and access status.",
            "Emulator readings describe the virtual device only."};
        const size_t count = std::max(1, limit - 3);
        pages(help.size(), count, "Help");
        Rows rows;
        for (size_t i = state.page * count; i < std::min(help.size(), (state.page + 1) * count); ++i) rows.emplace_back(help[i], "");
        append(box("KEYS & READINGS", rows, width, cyan));
    } else {
        if (state.view == 0) {
            append(overview(s, state, o, width, limit - 1, color));
        } else if (state.view == 1) {
            const int count = width >= 108 ? 4 : 2, w = (width - count + 1) / count;
            const int reserve = state.showProcesses && height >= 30 ? 9 : 0;
            const int allRows = std::max(1, static_cast<int>((s.cores.size() + count - 1) / count));
            const int h = state.graphs ? std::clamp((limit - 1 - reserve) / allRows - 4, 1, 4) : 0, panelHeight = h + 4;
            const int rows = std::max(1, (limit - 1 - reserve) / panelHeight);
            const size_t pageSize = rows * count;
            pages(s.cores.size(), pageSize, "Cores");
            for (int row = 0; row < rows; ++row) {
                std::vector<Panel> panels;
                for (int col = 0; col < count; ++col) {
                    size_t i = state.page * pageSize + row * count + col;
                    if (i >= s.cores.size()) { if (col && !panels.empty()) panels.push_back(Panel(panelHeight, std::string(w, ' '))); continue; }
                    const auto& core = s.cores[i];
                    panels.push_back(metric("Core " + core.first.substr(3), number(core.second) + "% active", state.coreHistory[core.first], 100, "%", w, h, cyan));
                }
                join(panels);
            }
        } else {
            append(hardwareView(s, state, o, width, limit - 1, color, context));
        }
        int available = limit - static_cast<int>(lines.size());
        if (state.showProcesses && available >= 7) {
            line(""); --available;
            auto processes = processList(s, state, o.sort);
            const int rowCount = std::min(std::max(1, static_cast<int>(processes.size())), available - 4), inner = width - 4;
            state.offset = std::min(state.offset, processes.size() > static_cast<size_t>(rowCount) ? processes.size() - rowCount : 0);
            const int nameWidth = inner - 29;
            auto row = [&](std::string pid, std::string name, std::string cpu, std::string mem, bool values = false) {
                return fit(pid, 6, true) + " " + fit(name, nameWidth) + " " + (values ? green : "") +
                    fit(cpu, 8, true) + " " + fit(mem, 12, true) + (values ? reset : "");
            };
            const bool pss = o.sort == Sort::Pss;
            Rows table{{row("PID", "PROCESS", "CPU %", pss ? "Memory (PSS)" : "Memory (RSS)"), bold + cyan}, {repeatGlyph(hz, inner), muted}};
            for (int i = 0; i < rowCount; ++i) {
                size_t index = state.offset + i;
                if (index >= processes.size()) { table.emplace_back("No matching processes", muted); continue; }
                const auto& p = processes[index];
                table.emplace_back(row(std::to_string(p.pid), p.name, number(p.cpu), memory(pss ? p.pss : p.rss), true), "");
            }
            std::string caption = "PROCESSES / " + std::to_string(processes.size()) + " visible / Sort: " + sortLabel(o.sort);
            if (pss && width >= 90) caption += " / PSS " + std::to_string(s.pssReadable) + " readable";
            append(box(caption, table, width, cyan));
            if (context.empty()) context = "s Sort: " + sortLabel(o.sort) + (width >= 90 ? " (high to low)  / Search  Up/Down Scroll" : "  / Search  Up/Down Scroll");
        }
    }
    if (state.editing) context = "/" + state.draft + "_  Enter apply / Esc cancel";
    else if (!s.error.empty()) context = s.error;
    else if (!state.notice.empty()) context = state.notice;
    else if (!state.filter.empty()) context = "Search: " + state.filter + "  Esc clear";
    line(context.empty() ? "" : " " + context, state.notice.empty() ? muted : yellow);
    std::string actions = std::string(" q Quit  p ") + (state.paused ? "Resume" : "Pause") + "  v View  t ";
    actions += width >= 90 ? (state.showProcesses ? "Hide processes" : "Show processes") : "Procs";
    actions += width >= 90 ? "  r Refresh  + Add time  - Less time  ? Help" : "  r Refresh  ? Help";
    line(actions, cyan);
    return lines;
}
