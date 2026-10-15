#include "model.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

using namespace hmon;
using Clock = std::chrono::steady_clock;
static volatile sig_atomic_t stopped = 0;
static void stop(int) { stopped = 1; }

static std::string readFile(const std::string& path, size_t limit = 262144) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string result;
    char buffer[4096];
    while (result.size() < limit) {
        ssize_t size = read(fd, buffer, std::min(sizeof(buffer), limit - result.size()));
        if (size <= 0) break;
        result.append(buffer, static_cast<size_t>(size));
    }
    close(fd);
    return result;
}
#include "command.hpp"
#include "thermal_service.hpp"
#include "thermal_channel.hpp"
#include "hardware.hpp"
#include "cpu_service.hpp"
class Collector {
    CpuMap previous;
    struct PreviousProcess { ProcessTicks ticks; Clock::time_point sampled; };
    std::map<int, PreviousProcess> previousProcesses;
    Hardware hardware;
    long hz = sysconf(_SC_CLK_TCK);
    bool emulator;
public:
    explicit Collector(bool emulatorMode) : emulator(emulatorMode) {}
    void reset() { previous.clear(); previousProcesses.clear(); }
    Snapshot collect(bool withPss) {
        const auto start = Clock::now();
        Snapshot result;
        result.timestamp = std::chrono::duration<double>(start.time_since_epoch()).count();
        std::map<int, PreviousProcess> currentProcesses;
        CpuMap current = parseCpu(readFile("/proc/stat"));
        for (const auto& [name, ticks] : current) {
            Value usage;
            if (previous.count(name)) usage = cpuDelta(previous.at(name), ticks);
            if (name == "cpu") result.cpu = usage;
            else result.cores.emplace_back(name, usage);
        }
        previous = std::move(current);
        std::sort(result.cores.begin(), result.cores.end(), [](const auto& a, const auto& b) {
            return std::stoul(a.first.substr(3)) < std::stoul(b.first.substr(3));
        });
        std::string memory = readFile("/proc/meminfo");
        result.total = kbField(memory, "MemTotal");
        result.available = kbField(memory, "MemAvailable");
        result.used = difference(result.total, result.available);
        result.swapTotal = kbField(memory, "SwapTotal");
        result.swapUsed = difference(result.swapTotal, kbField(memory, "SwapFree"));
        result.gpuMemory = kbField(memory, "GpuTotalUsed");
        if (previous.empty()) {
            result.error = "/proc/stat " + pathStatus("/proc/stat", false);
            if (emulator) {
                std::string dump = serviceCpuDump();
                result.cpu = cpuServicePercent(dump);
                if (result.cpu) {
                    result.cpuSource = "hidumper-cpu-service";
                    result.error.clear();
                    auto begin = dump.find("CPU usage from ");
                    if (begin != std::string::npos) result.cpuWindow = safeName(dump.substr(begin, dump.find('\n', begin) - begin));
                }
                std::string status = readFile("/proc/self/status", 16384);
                auto start = status.find("Cpus_allowed_list:");
                if (start != std::string::npos) {
                    auto ids = cpuIds(status.substr(start + 18, status.find('\n', start) - start - 18));
                    for (int id : ids) result.cores.emplace_back("cpu" + std::to_string(id), Value{});
                    result.coreSource = "process-affinity; per-core load unavailable";
                }
            }
        }
        if (!result.total) result.error = "/proc/meminfo unavailable; check native shell permissions";
        DIR* directory = opendir("/proc");
        if (directory) {
            while (dirent* entry = readdir(directory)) {
                std::string id(entry->d_name);
                if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) continue;
                std::string path = "/proc/" + id;
                std::string status = readFile(path + "/status", 32768);
                if (status.empty()) continue;
                Process p;
                try { p.pid = std::stoi(id); } catch (...) { continue; }
                // Display only argv[0], never the process command arguments.
                std::string command = readFile(path + "/cmdline", 4096);
                p.name = safeName(command);
                if (p.name.empty()) {
                    auto begin = status.find("Name:");
                    if (begin != std::string::npos) {
                        begin = status.find_first_not_of(" \t", begin + 5);
                        if (begin != std::string::npos) p.name = safeName(status.substr(begin, status.find('\n', begin) - begin));
                    }
                }
                if (p.name.empty()) p.name = "[unknown]";
                p.rss = kbField(status, "VmRSS");
                auto uidStart = status.find("\nUid:");
                if (uidStart != std::string::npos) {
                    std::istringstream uidField(status.substr(uidStart + 5)); std::string uid;
                    uidField >> uid; auto parsed = unsignedInteger(uid); if (parsed) p.uid = static_cast<double>(*parsed);
                }
                auto ticks = parseProcessTicks(readFile(path + "/stat", 8192));
                const auto sampled = Clock::now();
                if (ticks) {
                    auto old = previousProcesses.find(p.pid);
                    if (old != previousProcesses.end())
                        p.cpu = processCpu(old->second.ticks, *ticks, std::chrono::duration<double>(sampled - old->second.sampled).count(), hz);
                    currentProcesses[p.pid] = {*ticks, sampled};
                }
                if (withPss) {
                    p.pss = kbField(readFile(path + "/smaps_rollup", 32768), "Pss");
                    if (p.pss) ++result.pssReadable;
                }
                result.processes.push_back(std::move(p));
            }
            closedir(directory);
        }
        previousProcesses = std::move(currentProcesses);
        hardware.collect(result);
        if (result.processes.empty()) result.error = "No visible processes; check shell permissions";
        std::time_t now = std::time(nullptr);
        char time[32];
        std::strftime(time, sizeof(time), "%H:%M:%S", std::localtime(&now));
        result.time = time;
        result.costMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return result;
    }
};

#include "format.hpp"
class Terminal {
    termios saved{};
    bool raw = false, active = false;
public:
    explicit Terminal(bool enabled) {
        if (!enabled) return;
        if (tcgetattr(STDIN_FILENO, &saved) != 0) throw std::runtime_error("Cannot read terminal settings; use --once");
        termios current = saved;
        current.c_lflag &= ~(ICANON | ECHO);
        current.c_cc[VMIN] = 0;
        current.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &current) != 0) throw std::runtime_error("Cannot set terminal mode");
        raw = true;
        std::cout << "\033[?1049h\033[?25l\033[2J" << std::flush;
        active = true;
    }
    ~Terminal() {
        if (active) std::cout << "\033[0m\033[?25h\033[?1049l" << std::flush;
        if (raw) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    }
};
static std::vector<Process> processList(const Snapshot& snapshot, const State& state, Sort sort) {
    std::vector<Process> result;
    for (const Process& p : snapshot.processes) if (matches(p, state.editing ? state.draft : state.filter)) result.push_back(p);
    std::sort(result.begin(), result.end(), [sort](const Process& a, const Process& b) { return processLess(a, b, sort); });
    return result;
}

// Render complete lines into one write; line erasure handles shrinking values/resizes without flicker.
#include "ui.hpp"
static void render(const Snapshot& s, State& state, const Options& options, bool interactive) {
    int width = options.width, height = options.height;
    winsize size{};
    if (interactive && ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col && size.ws_row) {
        width = size.ws_col; height = size.ws_row;
    }
    auto lines = dashboard(s, state, options, width, height, interactive);
    std::ostringstream frame;
    if (interactive) frame << "\033[H";
    for (const auto& line : lines) frame << line << (interactive ? "\033[K\r\n" : "\n");
    if (interactive) frame << "\033[J";
    std::cout << frame.str() << std::flush;
}
static std::string jsonString(const std::string& s) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else { if (c == '\\' || c == '"') out += '\\'; out += static_cast<char>(c); }
    }
    return out + '"';
}
static std::string jsonValue(Value v) { return v ? number(v, 3) : "null"; }
static void json(const Snapshot& s, const State& state, const Options& options) {
    std::cout << "{\"schemaVersion\":2,\"source\":\"native-proc\",\"environment\":" << jsonString(options.emulator ? "emulator-test" : "native-harmonyos")
              << ",\"timestampMonotonicSeconds\":" << number(s.timestamp, 6) << ",\"uid\":" << getuid()
              << ",\"cpuPercent\":" << jsonValue(s.cpu) << ",\"cpuSource\":" << jsonString(s.cpuSource)
              << ",\"cpuWindow\":" << jsonString(s.cpuWindow) << ",\"coreSource\":" << jsonString(s.coreSource) << ",\"memoryTotalKb\":" << jsonValue(s.total)
              << ",\"memoryUsedKb\":" << jsonValue(s.used) << ",\"memoryAvailableKb\":" << jsonValue(s.available)
              << ",\"swapTotalKb\":" << jsonValue(s.swapTotal) << ",\"swapUsedKb\":" << jsonValue(s.swapUsed)
              << ",\"gpuMemoryKb\":" << jsonValue(s.gpuMemory) << ",\"gpuPercent\":" << jsonValue(s.gpuPercent)
              << ",\"gpuFrequencyMHz\":" << jsonValue(s.gpuMHz) << ",\"gpuSource\":" << jsonString(s.gpuSource)
              << ",\"cpuPowerW\":null,\"gpuPowerW\":null,\"npuPowerW\":null,\"cpuScaledRatio\":null,\"sampleMs\":" << number(s.costMs, 3)
              << ",\"visibleProcesses\":" << s.processes.size() << ",\"pssEnabled\":" << (options.pss ? "true" : "false") << ",\"cores\":[";
    bool first = true;
    for (const auto& [id, value] : s.cores) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":" << jsonString(id) << ",\"percent\":" << jsonValue(value) << '}';
    }
    std::cout << "],\"processes\":[";
    first = true;
    for (const auto& p : processList(s, state, options.sort)) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"pid\":" << p.pid << ",\"name\":" << jsonString(p.name) << ",\"rssKb\":" << jsonValue(p.rss) << ",\"pssKb\":" << jsonValue(p.pss)
                  << ",\"cpuPercent\":" << jsonValue(p.cpu) << ",\"uid\":" << jsonValue(p.uid) << ",\"gpuPercent\":null,\"powerW\":null}";
    }
    std::cout << "],\"groups\":["; first = true;
    for (const auto& g : s.groups) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":" << jsonString(g.id) << ",\"activePercent\":" << jsonValue(g.active) << ",\"frequencyMHz\":" << jsonValue(g.mhz)
                  << ",\"frequencyKind\":" << jsonString(g.frequencyKind) << ",\"scaledRatio\":null,\"source\":" << jsonString(g.source) << ",\"cores\":[";
        for (size_t i = 0; i < g.cores.size(); ++i) { if (i) std::cout << ','; std::cout << g.cores[i]; }
        std::cout << "]}";
    }
    std::cout << "],\"sensors\":["; first = true;
    for (const auto& v : s.sensors) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"name\":" << jsonString(v.name) << ",\"value\":" << jsonValue(v.value) << ",\"unit\":" << jsonString(v.unit)
                  << ",\"kind\":" << jsonString(v.kind) << ",\"source\":" << jsonString(v.path)
                  << ",\"rawValue\":" << jsonValue(v.raw) << ",\"status\":" << jsonString(v.status) << '}';
    }
    std::cout << "],\"capabilities\":["; first = true;
    for (const auto& c : s.capabilities) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"name\":" << jsonString(c.name) << ",\"status\":" << jsonString(c.status) << ",\"path\":" << jsonString(c.path) << '}';
    }
    std::cout << "],\"error\":" << jsonString(s.error) << "}\n" << std::flush;
}
static void handleKey(char key, State& state, Options& options, Collector& collector) {
    if (state.editing) {
        if (key == 27) state.editing = false;
        else if (key == '\r' || key == '\n') { state.filter = state.draft; state.editing = false; }
        else if (key == 127 || key == 8) { if (!state.draft.empty()) state.draft.pop_back(); }
        else if (key >= 32 && key <= 126 && state.draft.size() < 128) state.draft += key;
        state.offset = 0;
        return;
    }
    auto notice = [&](const std::string& text) {
        state.notice = text;
        state.noticeUntil = std::chrono::duration<double>(Clock::now().time_since_epoch()).count() + 2.5;
    };
    switch (key) {
        case 'q': case 3: stopped = 1; break;
        case 'p': case ' ': state.paused = !state.paused; collector.reset(); state.gap(std::chrono::duration<double>(Clock::now().time_since_epoch()).count()); break;
        case '?': state.help = !state.help; state.page = 0; break;
        case '/': state.editing = true; state.draft = state.filter; break;
        case 27: state.filter.clear(); state.help = false; state.offset = 0; break;
        case 'm': options.pss = false; options.sort = Sort::Rss; state.offset = 0; break;
        case 's':
            options.sort = options.sort == Sort::Rss ? Sort::Cpu : options.sort == Sort::Cpu ? Sort::Pss : Sort::Rss;
            options.pss = options.sort == Sort::Pss; state.offset = 0; break;
        case 'c': options.pss = false; options.sort = Sort::Cpu; state.offset = 0; break;
        case 'v': state.view = (state.view + 1) % 3; state.page = 0; break;
        case 't': state.showProcesses = !state.showProcesses; break;
        case 'b': state.graphs = !state.graphs; break;
        case '[': if (state.page) --state.page; break;
        case ']': if (state.page + 1 < state.pageCount) ++state.page; break;
        case 'j': ++state.offset; break;
        case 'k': if (state.offset) --state.offset; break;
        case 'g': state.offset = 0; break;
        case '+': case '=': options.interval = std::min(10.0, options.interval + 0.5); break;
        case '-': case '_': options.interval = std::max(0.5, options.interval - 0.5); break;
        case '0': options.interval = 0.5; break;
        case '1': case '2': case '5': options.interval = key - '0'; break;
        case 'r': options.interval = options.interval < 1 ? 1 : options.interval < 2 ? 2 : options.interval < 5 ? 5 : 0.5; break;
    }
    if (key == 's' || key == 'm' || key == 'c')
        notice("Sort: " + sortLabel(options.sort) + ", high to low" + (state.showProcesses ? "" : " | t Show processes"));
    if (key == '+' || key == '=' || key == '-' || key == '_' || key == 'r' || key == '0' || key == '1' || key == '2' || key == '5')
        notice("Refresh every " + number(options.interval) + "s" + (options.interval == 0.5 ? " (fastest)" : options.interval == 10 ? " (slowest)" : "") + (state.paused ? " | Paused: p Resume" : ""));
}
#include "selftest.hpp"
static void usage() {
    std::cout << "hmon 0.2.7 - native HarmonyOS PC monitor\n"
      "Usage: hmon [--once | --json] [--samples N] [--interval seconds]\n"
      "            [--pss] [--filter text] [--ascii] [--no-color]\n"
      "            [--width N --height N] [--sort rss|pss|cpu] [--compact]\n"
      "            [--view overview|cores|hardware] [--diagnose] [--emulator]\n"
      "            [--self-test] [--help]\n"
      "Interactive: q quit, p pause, / search, s cycle sort, r refresh, + add time, - subtract time, v view, t processes, ? help\n"
      "Left/Right or [/] turn pages only when needed. c/m sort by CPU/RSS. b toggles charts.\n"
      "--once primes CPU for 200 ms, then prints one snapshot.\n"
      "--json prints newline-delimited snapshots; use --samples to bound output.\n"
      "Native HiShell by default. --emulator explicitly labels virtual-device test data.\n";
}
int main(int argc, char** argv) {
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            auto value = [&]() { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return std::string(argv[i]); };
            auto integer = [&]() { std::string text = value(); size_t end = 0; int v = std::stoi(text, &end); if (end != text.size()) throw std::runtime_error("Invalid " + arg); return v; };
            if (arg == "--help" || arg == "-h") { usage(); return 0; }
            else if (arg == "--version") { std::cout << "hmon 0.2.7\n"; return 0; }
            else if (arg == "--once") options.once = true;
            else if (arg == "--json") options.json = true;
            else if (arg == "--ascii") options.ascii = true;
            else if (arg == "--no-color") options.plain = true;
            else if (arg == "--pss") { options.pss = true; options.sort = Sort::Pss; }
            else if (arg == "--compact") options.compact = true;
            else if (arg == "--emulator") options.emulator = true;
            else if (arg == "--diagnose") options.diagnose = true;
            else if (arg == "--view") {
                std::string view = value();
                if (view == "overview") options.view = 0;
                else if (view == "cores") options.view = 1;
                else if (view == "hardware") options.view = 2;
                else throw std::runtime_error("view must be overview, cores or hardware");
            }
            else if (arg == "--sort") {
                std::string key = value();
                if (key == "rss") options.sort = Sort::Rss;
                else if (key == "cpu") options.sort = Sort::Cpu;
                else if (key == "pss") { options.sort = Sort::Pss; options.pss = true; }
                else throw std::runtime_error("sort must be rss, pss or cpu");
            }
            else if (arg == "--filter") options.filter = safeName(value());
            else if (arg == "--samples") { options.samples = integer(); if (options.samples < 1 || options.samples > 1000000) throw std::runtime_error("samples must be 1..1000000"); }
            else if (arg == "--width") { options.width = integer(); if (options.width < 1 || options.width > 1000) throw std::runtime_error("width must be 1..1000"); }
            else if (arg == "--height") { options.height = integer(); if (options.height < 4 || options.height > 500) throw std::runtime_error("height must be 4..500"); }
            else if (arg == "--self-test") options.selfTest = true;
            else if (arg == "--interval") {
                std::string text = value(); size_t end = 0; options.interval = std::stod(text, &end);
                if (end != text.size() || !std::isfinite(options.interval) || options.interval < 0.5 || options.interval > 10) throw std::runtime_error("interval must be 0.5..10 seconds");
            } else throw std::runtime_error("Unknown option: " + arg);
        }
        if (options.selfTest) return selfTest();
        utsname system{};
        if (uname(&system) != 0 || (std::string(system.sysname) != "HarmonyOS" && !options.emulator)) {
            std::cerr << "hmon requires native HarmonyOS HiShell. For emulator tests only, pass --emulator (virtual-device metrics).\n";
            return 2;
        }
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop); std::signal(SIGHUP, stop);
        // Keep terminal ownership for our lifetime; Ctrl+Z must not leave a hidden cursor/raw input behind.
        std::signal(SIGTSTP, SIG_IGN);
        bool interactive = !options.once && !options.json && !options.diagnose && isatty(0) && isatty(1);
        if (!interactive && !options.json && !options.samples) options.once = true;
        if (options.once) options.samples = 1;
        if (getenv("NO_COLOR")) options.plain = true;
        Terminal terminal(interactive);
        Collector collector(options.emulator);
        if (options.diagnose) {
            auto probe = collector.collect(false);
            std::cout << "hmon 0.2.7 diagnostics | " << system.sysname << " " << system.machine
                      << " | uid " << getuid() << (options.emulator ? " | EMULATOR / TEST" : " | native") << "\n";
            for (const auto& c : probe.capabilities) std::cout << c.name << ": " << c.status << " [" << c.path << "]\n";
            for (const auto& g : probe.groups) std::cout << g.id << ": " << number(g.mhz) << " MHz (" << g.frequencyKind << ") [" << g.source << "]\n";
            for (const auto& sensor : probe.sensors) std::cout << sensor.name << ": " << number(sensor.value) << " " << sensor.unit
                << " [" << sensor.path << "] " << sensor.status << " | raw " << number(sensor.raw, 0) << "\n";
            return 0;
        }
        State state; state.filter = options.filter; state.showProcesses = !options.compact; state.view = options.view;
        Snapshot snapshot;
        if (options.once) { collector.collect(false); std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
        auto next = Clock::now();
        int samples = 0;
        bool dirty = true;
        int escapeState = 0;
        auto escapeAt = Clock::now();
        while (!stopped) {
            if (!state.notice.empty() && std::chrono::duration<double>(Clock::now().time_since_epoch()).count() >= state.noticeUntil) {
                state.notice.clear(); dirty = true;
            }
            if (!state.paused && Clock::now() >= next) {
                auto sampleStart = Clock::now();
                snapshot = collector.collect(options.pss);
                state.record(snapshot);
                ++samples; dirty = true;
                // Schedule from the sampling start, not after its cost. Never queue catch-up samples.
                next = std::max(sampleStart + std::chrono::milliseconds(static_cast<int>(options.interval * 1000)), Clock::now() + std::chrono::milliseconds(10));
                if (options.json) json(snapshot, state, options);
            }
            if (dirty && !options.json) render(snapshot, state, options, interactive);
            dirty = false;
            if (options.samples && samples >= options.samples) break;
            if (interactive) {
                pollfd descriptor{STDIN_FILENO, POLLIN, 0};
                int timeout = state.paused ? 100 : std::clamp(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count()), 0, 100);
                int ready = poll(&descriptor, 1, timeout);
                if (ready > 0 && (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))) break;
                if (ready > 0 && (descriptor.revents & POLLIN)) {
                    char input[128]; ssize_t count = read(0, input, sizeof(input));
                    if (count == 0) break;
                    for (ssize_t i = 0; i < count; ++i) {
                        char c = input[i];
                        if (escapeState == 1 && c == '[') { escapeState = 2; continue; }
                        if (escapeState == 2) {
                            if (!state.editing) {
                                if (c == 'A') handleKey('k', state, options, collector);
                                else if (c == 'B') handleKey('j', state, options, collector);
                                else if (c == 'C') handleKey(']', state, options, collector);
                                else if (c == 'D') handleKey('[', state, options, collector);
                            }
                            escapeState = 0; continue;
                        }
                        if (escapeState == 1) { handleKey(27, state, options, collector); escapeState = 0; }
                        if (c == 27) { escapeState = 1; escapeAt = Clock::now(); continue; }
                        bool wasPaused = state.paused;
                        double oldInterval = options.interval;
                        bool oldPss = options.pss;
                        handleKey(c, state, options, collector);
                        if ((wasPaused && !state.paused) || oldInterval != options.interval || oldPss != options.pss) next = Clock::now();
                    }
                    dirty = true;
                }
                if (escapeState && Clock::now() - escapeAt > std::chrono::milliseconds(100)) {
                    if (escapeState == 1) handleKey(27, state, options, collector);
                    escapeState = 0; dirty = true;
                }
                // Redraw on resize, including while paused, without collecting a sample.
                static winsize last{}; winsize current{};
                if (ioctl(1, TIOCGWINSZ, &current) == 0 && (current.ws_col != last.ws_col || current.ws_row != last.ws_row)) { last = current; dirty = true; }
            } else std::this_thread::sleep_until(std::min(next, Clock::now() + std::chrono::milliseconds(100)));
        }
        return snapshot.error.empty() ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "hmon: " << error.what() << '\n';
        return 1;
    }
}
