#pragma once

static std::string trimSensorText(const std::string& text) {
    auto begin = text.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? "" : text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
static bool validSensorName(const std::string& name) {
    return !name.empty() && name.size() <= 64 && name.find_first_not_of(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}
static bool knownThermalType(const std::string& name) {
    // Service temperatures default to milli-Celsius. Non-temperature types share the same dump field.
    // Preserve original names; board / supply / case sensors are NOT CPU or GPU junction sensors.
    static const std::vector<std::string> types{
        "battery", "ambient", "ddr_type", "drmos_cpub", "drmos_gpu_npu", "rfboard",
        "shell", "shell_back", "shell_frame", "shell_front", "system_h", "type_c",
        "soc-thermal", "soc_thermal", "cpu-thermal", "cpu_thermal", "gpu-thermal", "gpu_thermal",
        "npu-thermal", "npu_thermal"};
    return std::find(types.begin(), types.end(), name) != types.end();
}
static std::vector<Sensor> thermalServiceSensors(const std::string& text) {
    std::map<std::string, Sensor> found;
    std::istringstream lines(text); std::string line, name;
    while (std::getline(lines, line)) {
        line = trimSensorText(line);
        if (line.rfind("Type:", 0) == 0) {
            name = trimSensorText(line.substr(5));
            if (!validSensorName(name)) name.clear();
        } else if (line.rfind("Temperature:", 0) == 0 && !name.empty()) {
            if (found.size() >= 64 && !found.count(name)) { name.clear(); continue; }
            std::string value = trimSensorText(line.substr(12)), lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            Sensor sensor{name, "", "ThermalService/" + name, "unknown", {}};
            sensor.raw = hmon::scalar(value);
            if (value.empty() || value.find_first_not_of("-0123456789") != std::string::npos ||
                (sensor.raw && (*sensor.raw < -2147483648.0 || *sensor.raw > 2147483647.0))) sensor.raw.reset();
            const bool fan = lower.rfind("fan", 0) == 0;
            if (fan) {
                sensor.kind = "fan";
                sensor.status = "raw only; unit and validity unverified";
            } else if (knownThermalType(lower)) {
                sensor.kind = "temperature"; sensor.unit = "C";
                if (sensor.raw && *sensor.raw != -1 && *sensor.raw >= -40000 && *sensor.raw <= 200000) {
                    sensor.value = *sensor.raw / 1000;
                    sensor.status = "readable; ThermalService milli-Celsius";
                } else sensor.status = "invalid temperature value";
            } else sensor.status = "raw only; unknown sensor type";
            if (!sensor.raw) sensor.status = "invalid raw value";
            auto old = found.find(name);
            if (old == found.end()) found.emplace(name, sensor);
            else { old->second.value.reset(); old->second.raw.reset(); old->second.status = "duplicate sensor; rejected"; }
            name.clear();
        } else if (!line.empty()) name.clear();
    }
    std::vector<Sensor> result;
    for (auto& entry : found) result.push_back(std::move(entry.second));
    return result;
}
