#pragma once
#include <sys/stat.h>

static std::optional<CommandResult> thermalFrame(const std::string& text, unsigned long long sequence) {
    const std::string token = std::to_string(sequence);
    const std::string begin = "HMON BEGIN " + token + "\n", end = "\nHMON END " + token + " ";
    auto start = text.find(begin);
    if (start == std::string::npos) return {};
    start += begin.size();
    auto finish = text.find(end, start), statusEnd = finish == std::string::npos ? finish : text.find('\n', finish + end.size());
    if (finish == std::string::npos || statusEnd == std::string::npos) return {};
    std::string code = text.substr(finish + end.size(), statusEnd - finish - end.size());
    if (code != "0") return CommandResult{{}, "HiShell helper failed: " + hmon::safeName(code.substr(0, 16)), false};
    return CommandResult{text.substr(start, finish - start), "readable; HiShell channel", true};
}
class ThermalChannel {
    int request = -1, response = -1;
    unsigned long long sequence = 0;
    bool waiting = false;
    std::string buffer;
    bool drain() {
        char bytes[4096];
        for (;;) {
            ssize_t n = read(response, bytes, sizeof(bytes));
            if (n > 0) {
                if (buffer.size() + static_cast<size_t>(n) > 65536) return false;
                buffer.append(bytes, n);
            } else if (n == 0) return false;
            else if (errno == EAGAIN) return true;
            else if (errno != EINTR) return false;
        }
    }
public:
    ThermalChannel() {
        if (!getenv("HMON_THERMAL_CHANNEL")) return;
        struct stat a{}, b{};
        if (fstat(3, &a) != 0 || fstat(4, &b) != 0 || !S_ISFIFO(a.st_mode) || !S_ISFIFO(b.st_mode)) return;
        request = 3; response = 4;
        for (int fd : {request, response}) {
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        }
        std::signal(SIGPIPE, SIG_IGN);
    }
    bool available() const { return request >= 0; }
    CommandResult collect() {
        if (!drain()) return {{}, "HiShell channel closed / output limit", false};
        if (waiting) {
            if (!thermalFrame(buffer, sequence)) return {{}, "waiting for timed-out helper; no queued requests", false};
            // Discard an old response. Never mark a late sample as a fresh measurement.
            waiting = false;
        }
        buffer.clear(); ++sequence;
        std::string command = std::to_string(sequence) + '\n';
        if (write(request, command.data(), command.size()) != static_cast<ssize_t>(command.size()))
            return {{}, "HiShell channel unavailable", false};
        waiting = true;
        auto deadline = Clock::now() + std::chrono::milliseconds(350);
        do {
            if (!drain()) return {{}, "HiShell channel closed / output limit", false};
            auto result = thermalFrame(buffer, sequence);
            if (result) { waiting = false; buffer.clear(); return *result; }
            pollfd fd{response, POLLIN, 0}; poll(&fd, 1, 5);
        } while (Clock::now() < deadline && !stopped);
        return {{}, "HiShell helper timed out", false};
    }
};
