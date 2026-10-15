#pragma once
// Emulator fallback only; fixed command with bounded output, deadline and exit-status validation.
static std::string serviceCpuDump() {
    return readCommand(systemTool("hidumper").c_str(), {"--cpuusage"}, 350, 262144).output;
}
