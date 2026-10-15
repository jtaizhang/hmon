#pragma once
#include "metrics.hpp"
#include <deque>

namespace hmon {
struct History {
    std::deque<Value> values;
    std::deque<double> times;
    void push(Value value, double seconds = 0) {
        if (value && !std::isfinite(*value)) value.reset();
        values.push_back(value); times.push_back(seconds);
        if (values.size() > 120) { values.pop_front(); times.pop_front(); }
    }
    std::pair<Value, Value> summary() const {
        double sum = 0, maximum = 0; size_t count = 0;
        for (Value v : values) if (v) { sum += *v; maximum = count ? std::max(maximum, *v) : *v; ++count; }
        return count ? std::make_pair(Value(sum / count), Value(maximum)) : std::make_pair(Value{}, Value{});
    }
    double span() const { return times.size() > 1 ? std::max(0.0, times.back() - times.front()) : 0; }
    // Display all retained samples. Narrow windows average adjacent samples; a missing sample leaves a gap.
    std::vector<Value> columns(int width) const {
        std::vector<Value> out(std::max(0, width));
        if (width <= 0) return out;
        size_t count = std::min<size_t>(width, values.size());
        for (size_t x = 0; x < count; ++x) {
            size_t begin = x * values.size() / count, end = (x + 1) * values.size() / count;
            double sum = 0; bool missing = false;
            for (size_t i = begin; i < end; ++i) { if (values[i]) sum += *values[i]; else missing = true; }
            if (!missing) out[width - count + x] = sum / (end - begin);
        }
        return out;
    }
    std::vector<std::string> graph(int width, int height, double ceiling, bool ascii) const {
        std::vector<std::string> rows(std::max(0, height));
        const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        if (!(ceiling > 0)) ceiling = 1;
        for (Value v : columns(width)) {
            double cells = v ? std::clamp(*v / ceiling, 0.0, 1.0) * height : 0;
            for (int y = 0; y < height; ++y) {
                int eighths = static_cast<int>(std::round(std::clamp(cells - (height - 1 - y), 0.0, 1.0) * 8));
                rows[y] += ascii ? (eighths >= 4 ? "#" : eighths ? "." : " ") : blocks[eighths];
            }
        }
        return rows;
    }
};
} // namespace hmon
