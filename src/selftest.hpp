#pragma once
static int selfTest() {
    int count = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); ++count; };
    auto a = parseCpu("cpu 100 0 0 100 0 0 0 0 80 0\ncpu12 1 2 3 4\n");
    auto b = parseCpu("cpu 150 0 0 150 0 0 0 0 110 0\n");
    check(cpuDelta(a.at("cpu"), b.at("cpu")) == Value(50), "CPU guest double count");
    check(!cpuDelta(a.at("cpu"), a.at("cpu")), "Zero delta");
    check(!cpuDelta(b.at("cpu"), a.at("cpu")), "Counter reset");
    check(a.count("cpu12") == 1, "CPU index");
    check(parseCpu("cpu0 -1 2 3 4\ncpu1 1 2\ncpu2 1 2 3 4 broken\n").empty(), "Malformed CPU");
    check(kbField("Pss: 12 kB\nSwapPss: 20 kB\n", "Pss") == Value(12), "PSS separation");
    check(!kbField("VmRSS: -1 kB\n", "VmRSS"), "Negative RSS");
    check(!kbField("Name: x\n", "VmRSS"), "Unavailable RSS");
    check(difference(100, 20) == Value(80) && !difference(20, 100), "Memory accounting");
    check(safeName(std::string("app\0secret", 10)) == "app", "Command argument exclusion");
    check(safeName("a\033[2J\nb") == "a?[2J?b", "Terminal injection");
    Process p1{1, "Browser", 4, {}, 120, 100}, p2{2, "app", 0, 0, 0, 100};
    check(processLess(p1, p2, Sort::Rss) && processLess(p2, p1, Sort::Pss), "Null sort / real zero");
    check(processLess(p1, p2, Sort::Cpu), "CPU sorting");
    check(matches(p1, "brow") && matches(p1, "1") && !matches(p1, "xyz"), "Filtering");
    std::string proc = "123 (name with ) and ( brackets) R";
    for (int field = 4; field <= 22; ++field) proc += " " + std::to_string(field);
    auto ticks = parseProcessTicks(proc);
    check(ticks && ticks->user == 14 && ticks->system == 15 && ticks->start == 22, "Proc stat field positions / parentheses");
    check(!parseProcessTicks("123 (truncated) R 1 2"), "Truncated proc stat");
    check(!parseProcessTicks(proc.substr(0, proc.rfind(' ')) + " -1"), "Invalid starttime");
    ProcessTicks before{100, 20, 4}, after{300, 20, 4};
    check(processCpu(before, after, 1, 100) == Value(200), "Multicore process CPU");
    check(!processCpu(before, ProcessTicks{300, 20, 5}, 1, 100), "PID reuse");
    check(!processCpu(after, before, 1, 100), "Process counter rollback");
    check(!processCpu(before, after, 0, 100) && !processCpu(before, after, 1, 0), "Invalid CPU clock");
    check(cpuIds("0-3, 5 7-8") == std::vector<int>({0, 1, 2, 3, 5, 7, 8}), "Policy CPU list");
    check(cpuIds("3-1").empty() && cpuIds("0-99999").empty(), "Invalid CPU list");
    check(scalar("12345\n") == Value(12345) && !scalar("123 garbage") && !scalar("nan"), "Strict sensor values");
    check(cpuServicePercent("Details:\nTotal: 12.50%; User Space: 1%\n") == Value(12.5), "CPU service parser");
    check(!cpuServicePercent("Total: 150%;") && !cpuServicePercent("Total: --%;"), "Invalid CPU service values");
    auto thermal = thermalServiceSensors("Type: Battery\nTemperature: 30000\nType: fan_speed0\nTemperature: 0\nType: fan_speed1\nTemperature: 65535\nType: mystery\nTemperature: 42000\n");
    check(thermal.size() == 4 && thermal[0].value == Value(30) && thermal[0].raw == Value(30000), "Thermal service milli-Celsius");
    check(!thermal[1].value && thermal[1].raw == Value(0) && thermal[1].unit.empty() &&
          !thermal[2].value && thermal[2].raw == Value(65535), "Fan raw values are neither degrees nor verified RPM");
    check(!thermal[3].value && thermal[3].raw == Value(42000), "Unknown sensor is not inferred to be temperature");
    check(thermalServiceSensors("Temperature: 40000\nType: Battery\n").empty(), "Truncated thermal records");
    check(thermalServiceSensors("Type: Battery\nError\nTemperature: 30000\n").empty(), "Mismatched thermal records");
    auto badThermal = thermalServiceSensors("Type: Battery\nTemperature: nan\nType: shell_back\nTemperature: -1\nType: system_h\nTemperature: 2147483647\n");
    check(badThermal.size() == 3 && !badThermal[0].value && !badThermal[1].value && !badThermal[2].value, "Thermal invalid and sentinel values");
    auto duplicate = thermalServiceSensors("Type: Battery\nTemperature: 30000\nType: Battery\nTemperature: 31000\n");
    check(duplicate.size() == 1 && !duplicate[0].value && !duplicate[0].raw, "Duplicate thermal records rejected");
    check(thermalServiceSensors("Type: evil\033[2J\nTemperature: 30000\n").empty(), "Thermal label escape rejection");
    auto frame = thermalFrame("HMON BEGIN 1\nType: Battery\nTemperature: 30000\nHMON END 1 0\n", 1);
    check(frame && frame->ok && thermalServiceSensors(frame->output)[0].value == Value(30), "Thermal channel frame");
    check(!thermalFrame("HMON BEGIN 1\nold\nHMON END 1 0\n", 2), "Old thermal response rejected");
    check(!thermalFrame("HMON BEGIN 1\npartial\nHMON END 1 ", 1), "Truncated thermal response rejected");
    auto failedFrame = thermalFrame("HMON BEGIN 1\nType: Battery\nTemperature: 30000\nHMON END 1 124\n", 1);
    check(failedFrame && !failedFrame->ok && failedFrame->output.empty(), "Failed channel sample rejected");
    auto zeroThermal = thermalServiceSensors("Type: ambient\nTemperature: 0\n");
    check(zeroThermal.size() == 1 && zeroThermal[0].value == Value(0), "Valid zero temperature preserved");
    Snapshot firstThermal; firstThermal.sensors = thermal; firstThermal.timestamp = 1;
    State thermalState; thermalState.record(firstThermal);
    Snapshot missingThermal; missingThermal.timestamp = 2; thermalState.record(missingThermal);
    check(!thermalState.sensorHistory.at("ThermalService/Battery").values.back(), "Lost service sensor creates history gap");
    const std::string shell = systemTool("sh");
    auto failedCommand = readCommand(shell.c_str(), {"-c", "printf partial; exit 1"});
    check(!failedCommand.ok && failedCommand.output.empty() && failedCommand.status == "exit 1", "Failed helper output rejected");
    auto timeoutCommand = readCommand(shell.c_str(), {"-c", "while :; do :; done"}, 30);
    check(!timeoutCommand.ok && timeoutCommand.status == "timed out", "Helper timeout does not hang monitor");
    auto oversizedCommand = readCommand(shell.c_str(), {"-c", "printf 123456789"}, 250, 4);
    check(!oversizedCommand.ok && oversizedCommand.output.empty(), "Oversized helper output rejected");
    History history;
    for (int i = 0; i < 130; ++i) history.push(50, i);
    check(history.values.size() == 120 && history.span() == 119, "History bound / time span");
    check(history.summary().first == Value(50) && history.summary().second == Value(50), "History statistics");
    History gaps; gaps.push(0); gaps.push({}); gaps.push(100);
    check(gaps.summary().first == Value(50) && gaps.summary().second == Value(100), "Missing data excluded from statistics");
    check(!gaps.columns(1)[0] && gaps.columns(3)[0] == Value(0), "Resampled gap vs real zero");
    History full; full.push(100);
    check(full.graph(1, 3, 100, true) == std::vector<std::string>({"#", "#", "#"}), "Filled chart bounds");
    History zero; zero.push(0);
    check(zero.graph(1, 2, 100, false) == std::vector<std::string>({" ", " "}), "Zero chart not artificial load");
    check(jsonString("a\n\"\\") == "\"a\\u000a\\\"\\\\\"", "JSON escaping");
    Collector controlsCollector(true);
    State controls; Options controlOptions;
    handleKey('r', controls, controlOptions, controlsCollector);
    check(controlOptions.interval == 2 && controls.notice.find("2.0s") != std::string::npos, "Refresh preset and visible feedback");
    handleKey('r', controls, controlOptions, controlsCollector);
    handleKey('r', controls, controlOptions, controlsCollector);
    handleKey('-', controls, controlOptions, controlsCollector);
    check(controlOptions.interval == 0.5 && controls.notice.find("fastest") != std::string::npos, "Refresh lower bound");
    handleKey('+', controls, controlOptions, controlsCollector);
    check(controlOptions.interval == 1, "Slower refresh key");
    handleKey('5', controls, controlOptions, controlsCollector);
    check(controlOptions.interval == 5, "Direct interval selection");
    handleKey('_', controls, controlOptions, controlsCollector);
    check(controlOptions.interval == 4.5, "Shifted minus key");
    handleKey('s', controls, controlOptions, controlsCollector);
    check(controlOptions.sort == Sort::Cpu && !controlOptions.pss, "Sort cycle CPU");
    handleKey('s', controls, controlOptions, controlsCollector);
    check(controlOptions.sort == Sort::Pss && controlOptions.pss, "Sort cycle PSS collection");
    handleKey('s', controls, controlOptions, controlsCollector);
    check(controlOptions.sort == Sort::Rss && !controlOptions.pss, "Sort cycle RSS");
    controls.pageCount = 2;
    handleKey(']', controls, controlOptions, controlsCollector);
    handleKey(']', controls, controlOptions, controlsCollector);
    check(controls.page == 1, "Page upper bound");
    Snapshot sample;
    sample.cpu = 50; sample.total = 1024 * 1024; sample.used = 512 * 1024; sample.available = sample.used;
    sample.swapTotal = 0; sample.swapUsed = 0; sample.processes = {p1, p2}; sample.time = "12:34:56";
    sample.capabilities.push_back({"GPU", "", "no verified adapter"});
    sample.groups.push_back({"policy0", "/test/0", {0, 1, 2, 3}, 50, 1800, "hardware"});
    sample.groups.push_back({"policy1", "/test/1", {4, 5, 6, 7, 8, 9, 10, 11}, 70, 2100, "hardware"});
    sample.groups.push_back({"policy2", "/test/2", {12, 13}, 90, 1200, "requested"});
    sample.sensors.push_back({"Battery", "C", "ThermalService/Battery", "temperature", 30});
    sample.sensors.push_back({"ddr_type", "C", "ThermalService/ddr_type", "temperature", 45});
    sample.sensors.push_back({"shell_back", "C", "ThermalService/shell_back", "temperature", 38});
    for (int i = 0; i < 12; ++i) sample.sensors.push_back({"sensor" + std::to_string(i), "C", "/test/temp" + std::to_string(i), "temperature", 45});
    for (int i = 0; i < 14; ++i) sample.cores.emplace_back("cpu" + std::to_string(i), i * 7);
    State frequencyState; frequencyState.record(sample);
    check(frequencyState.groupFrequencyHistory.at("policy0").values.back() == Value(1800) &&
        frequencyState.groupHistory.at("policy0").values.back() == Value(50), "Frequency history is MHz, not CPU utilization");
    Snapshot nextFrequency = sample; nextFrequency.groups.clear(); nextFrequency.gpuMHz = 800; nextFrequency.timestamp = 1;
    frequencyState.record(nextFrequency);
    check(!frequencyState.groupFrequencyHistory.at("policy0").values.back() &&
        frequencyState.gpuFrequencyHistory.values.back() == Value(800), "Unavailable frequency leaves a gap");
    frequencyState.gap(2);
    check(!frequencyState.gpuFrequencyHistory.values.back(), "Pause inserts a frequency gap");
    check(terminalCells("温度 MHz █") == 10 && terminalCells("\033[36m温度\033[0m") == 4, "CJK width with chart glyph and ANSI color");
    check(cellFit("温度", 3) == "温 " && cellFit("温度", 5) == "温度 ", "Wide glyph clipping preserves cell alignment");
    check(temperatureName("drmos_cpub").label.find('*') != std::string::npos &&
        temperatureName("unidentified").label.find("未确认") != std::string::npos,
        "Unverified sensor locations are explicitly qualified");
    size_t layouts = 0;
    for (int w : {1, 40, 60, 61, 80, 100, 109, 120, 156, 157, 160, 241}) for (int h : {4, 6, 17, 18, 20, 24, 26, 30, 36, 60})
        for (int view = 0; view < 4; ++view) for (bool graphs : {false, true}) for (bool procs : {false, true}) {
            State state; state.record(sample); state.view = view % 3; state.help = view == 3;
            state.graphs = graphs; state.showProcesses = procs; state.offset = 999; state.page = 999;
            Options options; options.emulator = true;
            auto lines = dashboard(sample, state, options, w, h, false);
            bool valid = lines.size() <= static_cast<size_t>(h - 1);
            int tops = 0, bottoms = 0;
            for (const auto& line : lines) {
                const int cells = terminalCells(line);
                valid = valid && cells <= std::clamp(w - 1, 1, 240);
                for (size_t at = 0; (at = line.find("┌", at)) != std::string::npos; at += 3) ++tops;
                for (size_t at = 0; (at = line.find("└", at)) != std::string::npos; at += 3) ++bottoms;
            }
            if (!valid || tops != bottoms) throw std::runtime_error("Layout overflow / broken border: " + std::to_string(w) + "x" + std::to_string(h) + " view " + std::to_string(view));
            ++layouts;
        }
    ++count;
    // Vary actual topology and missing sensors around the desktop breakpoint, including ANSI colors.
    for (size_t groups : {0, 1, 2, 4}) for (int w : {80, 116, 117, 160}) for (int h : {18, 26, 36})
        for (bool charts : {false, true}) for (bool procs : {false, true}) for (bool colored : {false, true}) {
            Snapshot alternate = sample; alternate.groups.resize(groups); alternate.sensors.clear();
            State state; state.record(alternate); state.graphs = charts; state.showProcesses = procs;
            Options options;
            const auto output = dashboard(alternate, state, options, w, h, colored);
            if (output.size() > static_cast<size_t>(h - 1)) throw std::runtime_error("Overview height overflow");
            for (const auto& line : output) {
                int cells = 0;
                for (size_t i = 0; i < line.size(); ++i) {
                    if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
                        auto end = line.find('m', i + 2);
                        if (end == std::string::npos) throw std::runtime_error("Broken color sequence");
                        i = end; continue;
                    }
                    if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) ++cells;
                }
                if (cells != w - 1) throw std::runtime_error("Overview color/column alignment");
            }
            ++layouts;
        }
    ++count;
    // Every sensor remains reachable after grouping, including the final partially filled row.
    for (int w : {61, 118, 160}) for (int h : {18, 30, 60})
        for (bool charts : {false, true}) for (bool procs : {false, true}) for (bool colored : {false, true}) {
            State state; state.view = 2; state.record(sample); state.graphs = charts; state.showProcesses = procs;
            Options options; options.ascii = !colored;
            std::string allPages;
            for (size_t page = 0; page == 0 || page < state.pageCount; ++page) {
                state.page = page;
                const auto output = dashboard(sample, state, options, w, h, colored);
                if (output.size() > static_cast<size_t>(h - 1)) throw std::runtime_error("Hardware height overflow");
                for (const auto& line : output) {
                    if (terminalCells(line) != w - 1) throw std::runtime_error("Hardware CJK/color alignment");
                    allPages += line + '\n';
                }
                ++layouts;
            }
            for (const auto& sensor : sample.sensors)
                if (allPages.find(sensor.name) == std::string::npos) throw std::runtime_error("Hardware pagination lost a sensor");
            if (allPages.find("FREQUENCY") == std::string::npos || allPages.find("TEMPERATURE") == std::string::npos)
                throw std::runtime_error("Hardware sections missing");
        }
    ++count;
    State paged; paged.view = 1; paged.showProcesses = false; paged.record(sample);
    Options pageOptions;
    dashboard(sample, paged, pageOptions, 61, 18, false);
    check(paged.pageCount > 1, "Small core view has navigation");
    dashboard(sample, paged, pageOptions, 160, 60, false);
    check(paged.pageCount == 1 && paged.page == 0, "Resize removes obsolete navigation");
    std::cout << "PASS " << count << " checks; " << layouts << " layout combinations\n";
    return 0;
}
