#pragma once
#include <iomanip>
static std::string number(Value value, int precision = 1) {
    if (!value) return "--";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << *value;
    return out.str();
}
static std::string memory(Value kb) {
    if (!kb) return "--";
    return *kb >= 1048576 ? number(*kb / 1048576) + " GiB" : number(*kb / 1024) + " MiB";
}
static std::string fit(std::string text, int width, bool right = false) {
    width = std::max(width, 0);
    if (static_cast<int>(text.size()) > width) text = width > 1 ? text.substr(0, width - 1) + "~" : text.substr(0, width);
    return right ? std::string(width - text.size(), ' ') + text : text + std::string(width - text.size(), ' ');
}
static std::string memoryPair(Value used, Value total) {
    if (!total) return memory(used) + " / --";
    const bool gib = *total >= 1048576;
    const double scale = gib ? 1048576 : 1024;
    return number(used ? Value(*used / scale) : Value{}) + " / " + number(*total / scale) + (gib ? " GiB" : " MiB");
}
static std::string sortLabel(hmon::Sort sort) {
    return sort == hmon::Sort::Cpu ? "CPU" : sort == hmon::Sort::Pss ? "Memory (PSS)" : "Memory (RSS)";
}
static Value percent(Value used, Value total) {
    return used && total && *total > 0 ? Value(*used / *total * 100) : Value{};
}
