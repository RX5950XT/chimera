#pragma once

#include <string>
#include <vector>
#include <functional>
#include <windows.h>

namespace chimera::instance {

/**
 * @brief Cross-platform process launcher with pipe redirection.
 */
class ProcessLauncher {
public:
    struct Result {
        int exitCode;
        std::string stdoutText;
        std::string stderrText;
    };

    using OutputCallback = std::function<void(const std::string &line)>;

    static Result runSync(const std::string &exe, const std::vector<std::string> &args);

    static HANDLE runAsync(const std::string &exe, const std::vector<std::string> &args,
                           OutputCallback onStdout, OutputCallback onStderr,
                           bool startHidden = false,
                           DWORD initialPriorityClass = NORMAL_PRIORITY_CLASS);

    static bool isRunning(HANDLE hProcess);
    static bool terminate(HANDLE hProcess);
    static bool terminateProcessTreeById(DWORD rootPid);
    static int waitForExit(HANDLE hProcess, int timeoutMs);
    static void setProcessTreePriority(HANDLE hProcess, DWORD priorityClass);
    static void setProcessTreePriorityById(DWORD rootPid, DWORD priorityClass);
    static std::vector<std::string> visibleWindowTitlesInProcessTreeById(DWORD rootPid);

    // Exposed for testing — implements CommandLineToArgvW round-trip quoting rules
    static std::wstring quoteArg(const std::wstring &arg);
    static std::wstring utf8ToWide(const std::string &utf8);
};

} // namespace chimera::instance
