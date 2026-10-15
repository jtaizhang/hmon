#pragma once
#include "history.hpp"

struct CpuGroup {
    std::string id, source;
    std::vector<int> cores;
    hmon::Value active, mhz;
    std::string frequencyKind;
};
struct Sensor {
    std::string name, unit, path, kind;
    hmon::Value value;
    hmon::Value raw = {};
    std::string status = "";
};
struct Capability { std::string name, path, status; };
struct Snapshot {
    hmon::Value cpu, total, available, used, swapTotal, swapUsed, gpuMemory;
    hmon::Value gpuPercent, gpuMHz;
    std::string gpuSource;
    std::vector<std::pair<std::string, hmon::Value>> cores;
    std::vector<hmon::Process> processes;
    std::vector<CpuGroup> groups;
    std::vector<Sensor> sensors;
    std::vector<Capability> capabilities;
    double costMs = 0, timestamp = 0;
    std::string time, error, cpuSource = "proc-stat", coreSource = "proc-stat", cpuWindow;
    size_t pssReadable = 0;
};
struct Options {
    bool once = false, json = false, plain = false, ascii = false, pss = false, selfTest = false;
    bool diagnose = false, emulator = false, compact = false;
    int samples = 0, width = 80, height = 24, view = 0;
    double interval = 1;
    std::string filter;
    hmon::Sort sort = hmon::Sort::Rss;
};
struct State {
    bool paused = false, help = false, editing = false, showProcesses = true, graphs = true;
    int view = 0;
    std::string filter, draft, notice;
    double noticeUntil = 0;
    size_t offset = 0, page = 0, pageCount = 1;
    hmon::History cpuHistory, memoryHistory, swapHistory, gpuMemoryHistory, gpuHistory, gpuFrequencyHistory;
    std::map<std::string, hmon::History> coreHistory, groupHistory, groupFrequencyHistory, sensorHistory;
    void record(const Snapshot& s) {
        auto ratio = [](hmon::Value a, hmon::Value b) -> hmon::Value {
            if (!a || !b) return {};
            if (*b == 0 && *a == 0) return 0;
            return *b > 0 ? hmon::Value(*a / *b * 100) : hmon::Value{};
        };
        cpuHistory.push(s.cpu, s.timestamp); memoryHistory.push(ratio(s.used, s.total), s.timestamp);
        swapHistory.push(ratio(s.swapUsed, s.swapTotal), s.timestamp);
        gpuMemoryHistory.push(s.gpuMemory ? hmon::Value(*s.gpuMemory / 1024) : hmon::Value{}, s.timestamp);
        gpuHistory.push(s.gpuPercent, s.timestamp);
        gpuFrequencyHistory.push(s.gpuMHz, s.timestamp);
        for (auto& entry : coreHistory) {
            const std::string& id = entry.first;
            auto c = std::find_if(s.cores.begin(), s.cores.end(), [&](const auto& core) { return core.first == id; });
            entry.second.push(c == s.cores.end() ? hmon::Value{} : c->second, s.timestamp);
        }
        for (const auto& c : s.cores) if (!coreHistory.count(c.first)) coreHistory[c.first].push(c.second, s.timestamp);
        for (const auto& g : s.groups) groupHistory[g.id].push(g.active, s.timestamp);
        for (auto& history : groupFrequencyHistory) {
            auto group = std::find_if(s.groups.begin(), s.groups.end(), [&](const auto& g) { return g.id == history.first; });
            history.second.push(group == s.groups.end() ? hmon::Value{} : group->mhz, s.timestamp);
        }
        for (const auto& g : s.groups) if (!groupFrequencyHistory.count(g.id))
            groupFrequencyHistory[g.id].push(g.mhz, s.timestamp);
        // Missing service sensors must leave gaps, never retain a stale current measurement.
        for (auto& history : sensorHistory) {
            auto sensor = std::find_if(s.sensors.begin(), s.sensors.end(), [&](const auto& v) { return v.path == history.first; });
            history.second.push(sensor == s.sensors.end() ? hmon::Value{} : sensor->value, s.timestamp);
        }
        for (const auto& sensor : s.sensors) if (!sensorHistory.count(sensor.path))
            sensorHistory[sensor.path].push(sensor.value, s.timestamp);
    }
    void gap(double timestamp) {
        cpuHistory.push({}, timestamp); memoryHistory.push({}, timestamp);
        swapHistory.push({}, timestamp); gpuMemoryHistory.push({}, timestamp);
        gpuHistory.push({}, timestamp);
        gpuFrequencyHistory.push({}, timestamp);
        for (auto& p : coreHistory) p.second.push({}, timestamp);
        for (auto& p : groupHistory) p.second.push({}, timestamp);
        for (auto& p : groupFrequencyHistory) p.second.push({}, timestamp);
        for (auto& p : sensorHistory) p.second.push({}, timestamp);
    }
};
