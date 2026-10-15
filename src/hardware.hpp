#pragma once
#include <cerrno>

// Read-only sysfs adapters plus the documented ThermalService dump protocol.
// Presence on Linux/OpenHarmony does not imply availability on retail HarmonyOS hardware.
static std::vector<std::string> directoryEntries(const std::string& path) {
    std::vector<std::string> result;
    if (DIR* d = opendir(path.c_str())) {
        while (auto* e = readdir(d)) if (e->d_name[0] != '.') result.emplace_back(e->d_name);
        closedir(d);
    }
    std::sort(result.begin(), result.end());
    return result;
}
static std::string pathStatus(const std::string& path, bool directory) {
    int error = 0;
    if (directory) {
        DIR* d = opendir(path.c_str());
        if (d) { closedir(d); return "readable"; }
        error = errno;
    } else {
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd >= 0) { char c; ssize_t n = read(fd, &c, 1); error = errno; close(fd); if (n > 0) return "readable"; if (!n) return "empty"; }
        else error = errno;
    }
    if (error == EACCES || error == EPERM) return "permission denied";
    if (error == ENOENT || error == ENOTDIR) return "not present";
    return "read error " + std::to_string(error);
}
class Hardware {
    std::vector<CpuGroup> groups;
    std::vector<Sensor> sensors;
    std::vector<Capability> capabilities;
    ThermalChannel thermalChannel;
    const std::string gpuLoadPath = "/sys/class/devfreq/gpufreq/gpu_scene_aware/utilisation";
    const std::string gpuFrequencyPath = "/sys/class/devfreq/gpufreq/cur_freq";
    static std::string label(const std::string& path) {
        std::string s = readFile(path, 128);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return safeName(s);
    }
public:
    Hardware() {
        capabilities.push_back({"CPU tick counters", "/proc/stat", pathStatus("/proc/stat", false)});
        const std::string cpufreq = "/sys/devices/system/cpu/cpufreq";
        capabilities.push_back({"CPU frequency / policy groups", cpufreq, pathStatus(cpufreq, true)});
        for (const auto& entry : directoryEntries(cpufreq)) {
            if (entry.substr(0, 6) != "policy") continue;
            std::string path = cpufreq + "/" + entry;
            auto ids = cpuIds(readFile(path + "/related_cpus", 4096));
            if (ids.empty()) ids = cpuIds(readFile(path + "/affected_cpus", 4096));
            if (!ids.empty()) groups.push_back({entry, path, ids, {}, {}, ""});
        }
        const std::string thermal = "/sys/class/thermal";
        capabilities.push_back({"Thermal sensors", thermal, pathStatus(thermal, true)});
        for (const auto& entry : directoryEntries(thermal)) {
            if (entry.substr(0, 12) != "thermal_zone") continue;
            std::string path = thermal + "/" + entry;
            std::string name = label(path + "/type");
            sensors.push_back({name.empty() ? entry : name, "C", path + "/temp", "temperature", {}});
        }
        const std::string hwmon = "/sys/class/hwmon";
        capabilities.push_back({"Power / fan sensors", hwmon, pathStatus(hwmon, true)});
        for (const auto& entry : directoryEntries(hwmon)) {
            std::string path = hwmon + "/" + entry;
            for (const auto& file : directoryEntries(path)) {
                // hwmon power*_input is microwatts, fan*_input is RPM. No guessed voltage/current products.
                auto suffix = file.rfind("_input");
                bool power = file.substr(0, 5) == "power", fan = file.substr(0, 3) == "fan";
                if (suffix == std::string::npos || suffix + 6 != file.size() || (!power && !fan)) continue;
                auto index = unsignedInteger(file.substr(power ? 5 : 3, suffix - (power ? 5 : 3)));
                if (!index) continue;
                std::string name = label(path + "/" + file.substr(0, suffix) + "_label");
                if (name.empty()) name = file.substr(0, suffix);
                sensors.push_back({label(path + "/name") + ":" + name, power ? "W" : "RPM", path + "/" + file,
                                   power ? "power" : "fan", {}});
            }
        }
        for (const char* path : {"/sys/class/devfreq", "/sys/class/drm", "/sys/kernel/debug"})
            capabilities.push_back({"GPU / NPU candidate", path, pathStatus(path, true)});
        capabilities.push_back({"GamePerformance GPU API", "libgame_performance.z.so", "requires Init and app authentication; run ~/hmon/hmon-gpu-probe for live result"});
        capabilities.push_back({"GPU / NPU per-core counters", "", "no verified HarmonyOS driver adapter"});
        capabilities.push_back({"Per-process GPU / power", "", "no verified HarmonyOS attribution API"});
        capabilities.push_back({"Frequency-scaled active ratio", "", "requires active residency by frequency; instantaneous MHz is insufficient"});
    }
    void collect(Snapshot& s) {
        s.groups = groups; s.sensors = sensors; s.capabilities = capabilities;
        for (auto& group : s.groups) {
            double sum = 0; size_t count = 0;
            for (int id : group.cores) {
                auto it = std::find_if(s.cores.begin(), s.cores.end(), [&](const auto& c) { return c.first == "cpu" + std::to_string(id); });
                if (it != s.cores.end() && it->second) { sum += *it->second; ++count; }
            }
            if (count) group.active = sum / count;
            group.mhz = scalar(readFile(group.source + "/cpuinfo_cur_freq", 128));
            group.frequencyKind = "hardware";
            if (!group.mhz) { group.mhz = scalar(readFile(group.source + "/scaling_cur_freq", 128)); group.frequencyKind = "requested"; }
            if (group.mhz && *group.mhz > 0 && *group.mhz <= 10000000) *group.mhz /= 1000;
            else group.mhz.reset();
        }
        for (auto& sensor : s.sensors) {
            sensor.value = scalar(readFile(sensor.path, 128));
            sensor.raw = sensor.value;
            sensor.status = sensor.value ? "readable" : pathStatus(sensor.path, false);
            if (sensor.value) {
                if (sensor.unit == "C") *sensor.value /= 1000;
                if (sensor.unit == "W") *sensor.value /= 1000000;
                if ((sensor.unit == "C" && (*sensor.value < -40 || *sensor.value > 200)) ||
                    (sensor.unit != "C" && *sensor.value < 0)) {
                    sensor.value.reset(); sensor.status = "invalid sensor value";
                }
            }
        }
        // The same gpufreq paths are used by OpenHarmony's GpuCollectorImpl. Load is percent,
        // devfreq frequency is Hz; a failed read remains null instead of silently becoming zero.
        s.gpuSource = gpuLoadPath;
        s.gpuPercent = scalar(readFile(gpuLoadPath, 128));
        if (s.gpuPercent && (*s.gpuPercent < 0 || *s.gpuPercent > 100)) s.gpuPercent.reset();
        s.gpuMHz = scalar(readFile(gpuFrequencyPath, 128));
        if (s.gpuMHz && *s.gpuMHz > 0 && *s.gpuMHz <= 10000000000.0) *s.gpuMHz /= 1000000;
        else s.gpuMHz.reset();
        auto gpuStatus = [&](const std::string& path, hmon::Value value) {
            std::string status = pathStatus(path, false);
            return !value && status == "readable" ? "invalid numeric value" : status;
        };
        s.capabilities.push_back({"GPU load", gpuLoadPath, gpuStatus(gpuLoadPath, s.gpuPercent)});
        s.capabilities.push_back({"GPU frequency", gpuFrequencyPath, gpuStatus(gpuFrequencyPath, s.gpuMHz)});

        const std::string hidumper = systemTool("hidumper");
        auto dump = thermalChannel.available() ? thermalChannel.collect() : readCommand(hidumper.c_str(), {"-s", "ThermalService", "-a", "-t"});
        auto serviceSensors = thermalServiceSensors(dump.output);
        std::string serviceStatus = !dump.ok ? dump.status : serviceSensors.empty() ? "no sensor records / service access unavailable" : dump.status;
        if (!dump.ok && !thermalChannel.available()) serviceStatus += "; use the hmon launcher in HiShell (hmon-core cannot use its command proxy directly)";
        s.capabilities.push_back({"Temperature service", hidumper + " -s ThermalService -a -t", serviceStatus});
        for (auto& sensor : serviceSensors) {
            // Prefer a readable sysfs sensor with the same original label; retain service-only records.
            bool duplicate = std::any_of(s.sensors.begin(), s.sensors.end(), [&](const auto& other) {
                return other.name == sensor.name && other.kind == sensor.kind && other.value;
            });
            if (!duplicate) s.sensors.push_back(std::move(sensor));
        }
        for (const std::string kind : {"CPU", "GPU"}) {
            const std::string prefix = kind == "CPU" ? "cpu" : "gpu";
            bool available = std::any_of(s.sensors.begin(), s.sensors.end(), [&](const auto& sensor) {
                std::string name = sensor.name;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                return sensor.kind == "temperature" && sensor.value &&
                    (name == prefix || name == prefix + "-thermal" || name == prefix + "_thermal");
            });
            s.capabilities.push_back({kind + " temperature", "ThermalService / thermal zone",
                available ? "readable sensor label; not an average across cores" : "no readable, identified sensor; board/case temperature is not a substitute"});
        }
        bool fan = std::any_of(s.sensors.begin(), s.sensors.end(), [](const auto& sensor) {
            return sensor.kind == "fan" && sensor.value && sensor.unit == "RPM";
        });
        s.capabilities.push_back({"Fan RPM", "/sys/class/hwmon / ThermalService",
            fan ? "readable" : "no verified RPM source; service raw fan values are diagnostic only"});
    }
};
