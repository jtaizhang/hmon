#pragma once
#include <sys/wait.h>

struct CommandResult { std::string output, status; bool ok = false; };
static std::string systemTool(const std::string& name) {
    // Native HiShell exposes tools under /usr/bin; the emulator uses /system/bin.
    // Fixed system directories only: never execute a tool supplied via user PATH.
    for (const char* root : {"/usr/bin/", "/system/bin/", "/bin/"}) {
        std::string path = std::string(root) + name;
        if (access(path.c_str(), X_OK) == 0) return path;
    }
    return "/system/bin/" + name;
}
// Callers supply fixed, read-only commands. No shell expansion; output and lifetime are bounded.
static CommandResult readCommand(const char* executable, const std::vector<const char*>& args,
                                 int timeoutMs = 250, size_t limit = 65536) {
    int fds[2];
    if (pipe(fds) != 0) return {{}, "pipe failed", false};
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable));
    for (const char* arg : args) argv.push_back(const_cast<char*>(arg));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]); dup2(fds[1], STDOUT_FILENO); close(fds[1]);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, STDERR_FILENO); dup2(null, STDIN_FILENO); close(null); }
        execv(executable, argv.data());
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) { close(fds[0]); return {{}, "fork failed", false}; }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    CommandResult result;
    bool eof = false, exited = false, overflow = false;
    int status = 0;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline && !stopped) {
        char buffer[4096];
        ssize_t n = eof ? 0 : read(fds[0], buffer, sizeof(buffer));
        if (n > 0) {
            if (result.output.size() + static_cast<size_t>(n) > limit) { overflow = true; break; }
            result.output.append(buffer, n);
        } else if (n == 0) eof = true;
        else if (errno != EAGAIN && errno != EINTR) { result.status = "read failed"; break; }
        if (!exited) exited = waitpid(pid, &status, WNOHANG) == pid;
        if (eof && exited) break;
        if (n <= 0) { pollfd fd{fds[0], POLLIN, 0}; poll(&fd, eof ? 0 : 1, 5); }
    }
    close(fds[0]);
    if (!exited) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    result.ok = eof && exited && WIFEXITED(status) && WEXITSTATUS(status) == 0 && !overflow;
    if (result.ok) result.status = "readable";
    else if (overflow) result.status = "output limit exceeded";
    else if (!exited || !eof) result.status = stopped ? "interrupted" : "timed out";
    else if (WIFEXITED(status)) result.status = "exit " + std::to_string(WEXITSTATUS(status));
    else result.status = "helper terminated";
    if (!result.ok) result.output.clear(); // Never publish partial or failed output as current measurements.
    return result;
}
