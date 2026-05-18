#include "ProcessLauncher.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <array>
#include <cstdlib>
#include <tlhelp32.h>

namespace chimera::instance {

namespace {

std::vector<DWORD> collectChildProcesses(DWORD rootPid) {
    std::vector<std::pair<DWORD, DWORD>> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::vector<DWORD> result;
    std::vector<DWORD> queue{rootPid};
    for (size_t i = 0; i < queue.size(); ++i) {
        const DWORD parent = queue[i];
        for (const auto &[pid, parentPid] : processes) {
            if (parentPid == parent) {
                result.push_back(pid);
                queue.push_back(pid);
            }
        }
    }
    return result;
}

void terminateProcessId(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!process) return;
    TerminateProcess(process, 1);
    WaitForSingleObject(process, 3000);
    CloseHandle(process);
}

void applyPriority(DWORD pid, DWORD priorityClass) {
    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!process) return;
    SetPriorityClass(process, priorityClass);
    CloseHandle(process);
}

// Drain pipe into out; closes h when done.
void drainPipe(HANDLE h, std::string &out) {
    char buf[4096];
    DWORD read;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0)
        out.append(buf, read);
    CloseHandle(h);
}

bool useLegacyLauncher() {
    const char *env = std::getenv("CHIMERA_PROCESS_LAUNCHER");
    return env && std::string(env) == "legacy";
}

// Legacy runSync using _popen — kept for rollback via CHIMERA_PROCESS_LAUNCHER=legacy
ProcessLauncher::Result runSyncLegacy(const std::string &exe, const std::vector<std::string> &args) {
    ProcessLauncher::Result res{ -1, {}, {} };
    std::string cmd = "\"" + exe + "\"";
    for (const auto &a : args) cmd += " \"" + a + "\"";
    FILE *pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return res;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe))
        res.stdoutText += buffer;
    res.exitCode = _pclose(pipe);
    return res;
}

// Legacy runAsync command-line builder (naive quoting, no escape for embedded " or trailing \)
std::wstring buildCmdLineLegacy(const std::string &exe, const std::vector<std::string> &args) {
    std::wstring cmdLine = L"\"";
    {
        const std::wstring wexe = ProcessLauncher::utf8ToWide(exe);
        cmdLine += wexe;
    }
    cmdLine += L"\"";
    for (const auto &a : args) {
        cmdLine += L" \"";
        cmdLine += ProcessLauncher::utf8ToWide(a);
        cmdLine += L"\"";
    }
    return cmdLine;
}

} // namespace

// --- Public helpers ---

std::wstring ProcessLauncher::utf8ToWide(const std::string &utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wide(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), wlen);
    return wide;
}

// Implements CommandLineToArgvW round-trip quoting (Raymond Chen's documented rules):
//   2n backslashes + "  →  n backslashes + start/end quote toggle
//   2n+1 backslashes + "  →  n backslashes + literal "
//   n backslashes (not followed by ")  →  n literal backslashes
std::wstring ProcessLauncher::quoteArg(const std::wstring &arg) {
    // Plain word with no spaces or quotes needs no quoting
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return arg;

    std::wstring result;
    result.reserve(arg.size() + 4);
    result += L'"';

    for (auto it = arg.cbegin(); ; ++it) {
        int backslashes = 0;
        while (it != arg.cend() && *it == L'\\') {
            ++backslashes;
            ++it;
        }
        if (it == arg.cend()) {
            // At end of string: backslashes before closing " must be doubled
            result.append(static_cast<size_t>(backslashes) * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            // Before a literal ": double backslashes + escape the quote
            result.append(static_cast<size_t>(backslashes) * 2, L'\\');
            result += L"\\\"";
        } else {
            // Ordinary character: backslashes are literal
            result.append(static_cast<size_t>(backslashes), L'\\');
            result += *it;
        }
    }

    result += L'"';
    return result;
}

// --- runSync ---

ProcessLauncher::Result ProcessLauncher::runSync(const std::string &exe, const std::vector<std::string> &args) {
    if (useLegacyLauncher())
        return runSyncLegacy(exe, args);

    Result res{ -1, {}, {} };

    // Build properly-quoted wide command line
    std::wstring cmdLine = quoteArg(utf8ToWide(exe));
    for (const auto &a : args) {
        cmdLine += L' ';
        cmdLine += quoteArg(utf8ToWide(a));
    }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    HANDLE hErrRead = nullptr, hErrWrite = nullptr;

    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) return res;
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        return res;
    }
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_HIDE;
    si.hStdOutput   = hOutWrite;
    si.hStdError    = hErrWrite;
    si.hStdInput    = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr,
        /*inheritHandles=*/TRUE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        nullptr, nullptr, &si, &pi);

    // Close write ends before reading — required so ReadFile knows when child exits
    CloseHandle(hOutWrite);
    CloseHandle(hErrWrite);

    if (!created) {
        CloseHandle(hOutRead);
        CloseHandle(hErrRead);
        return res;
    }
    CloseHandle(pi.hThread);

    // Drain stdout and stderr concurrently to avoid pipe-buffer deadlock
    std::string stderrBuf;
    std::thread stderrThread([&]() { drainPipe(hErrRead, stderrBuf); });
    drainPipe(hOutRead, res.stdoutText);
    stderrThread.join();

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = static_cast<DWORD>(-1);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    res.exitCode  = static_cast<int>(exitCode);
    res.stderrText = std::move(stderrBuf);
    return res;
}

// --- runAsync ---

HANDLE ProcessLauncher::runAsync(const std::string &exe, const std::vector<std::string> &args,
                                  OutputCallback onStdout, OutputCallback onStderr,
                                  bool startHidden) {
    // Build command line — use proper quoting unless legacy mode requested
    std::wstring cmdLine;
    if (useLegacyLauncher()) {
        cmdLine = buildCmdLineLegacy(exe, args);
    } else {
        cmdLine = quoteArg(utf8ToWide(exe));
        for (const auto &a : args) {
            cmdLine += L' ';
            cmdLine += quoteArg(utf8ToWide(a));
        }
    }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    HANDLE hStdErrRead = nullptr, hStdErrWrite = nullptr;

    const bool redirect = (onStdout != nullptr) || (onStderr != nullptr);

    if (redirect) {
        if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) return nullptr;
        if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(hStdOutRead); CloseHandle(hStdOutWrite); return nullptr;
        }
        if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0)) {
            CloseHandle(hStdOutRead); CloseHandle(hStdOutWrite); return nullptr;
        }
        if (!SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(hStdOutRead); CloseHandle(hStdOutWrite);
            CloseHandle(hStdErrRead); CloseHandle(hStdErrWrite); return nullptr;
        }
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (redirect) {
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdOutWrite;
        si.hStdError  = hStdErrWrite;
    }
    if (startHidden) {
        si.dwFlags    |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr, cmdBuf.data(), nullptr, nullptr,
        redirect ? TRUE : FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        nullptr, nullptr, &si, &pi);

    if (redirect) {
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);
    }

    if (!created) {
        if (redirect) {
            CloseHandle(hStdOutRead);
            CloseHandle(hStdErrRead);
        }
        return nullptr;
    }

    CloseHandle(pi.hThread);

    if (redirect) {
        if (onStdout && hStdOutRead) {
            std::thread([hStdOutRead, onStdout]() {
                char buf[4096];
                DWORD read;
                std::string line;
                while (ReadFile(hStdOutRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
                    buf[read] = '\0';
                    line += buf;
                    size_t pos;
                    while ((pos = line.find('\n')) != std::string::npos) {
                        std::string l = line.substr(0, pos);
                        if (!l.empty() && l.back() == '\r') l.pop_back();
                        onStdout(l);
                        line = line.substr(pos + 1);
                    }
                }
                if (!line.empty()) onStdout(line);
                CloseHandle(hStdOutRead);
            }).detach();
        } else if (hStdOutRead) {
            CloseHandle(hStdOutRead);
        }

        if (onStderr && hStdErrRead) {
            std::thread([hStdErrRead, onStderr]() {
                char buf[4096];
                DWORD read;
                std::string line;
                while (ReadFile(hStdErrRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
                    buf[read] = '\0';
                    line += buf;
                    size_t pos;
                    while ((pos = line.find('\n')) != std::string::npos) {
                        std::string l = line.substr(0, pos);
                        if (!l.empty() && l.back() == '\r') l.pop_back();
                        onStderr(l);
                        line = line.substr(pos + 1);
                    }
                }
                if (!line.empty()) onStderr(line);
                CloseHandle(hStdErrRead);
            }).detach();
        } else if (hStdErrRead) {
            CloseHandle(hStdErrRead);
        }
    }

    return pi.hProcess;
}

bool ProcessLauncher::isRunning(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return false;
    DWORD code;
    if (GetExitCodeProcess(hProcess, &code))
        return code == STILL_ACTIVE;
    return false;
}

bool ProcessLauncher::terminate(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return false;
    const DWORD rootPid = GetProcessId(hProcess);
    auto children = collectChildProcesses(rootPid);
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        terminateProcessId(*it);
    return TerminateProcess(hProcess, 1) == TRUE;
}

int ProcessLauncher::waitForExit(HANDLE hProcess, int timeoutMs) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return -1;
    DWORD result = WaitForSingleObject(hProcess,
        timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
    if (result == WAIT_OBJECT_0) {
        DWORD code;
        if (GetExitCodeProcess(hProcess, &code)) {
            CloseHandle(hProcess);
            return static_cast<int>(code);
        }
    }
    return -1;
}

void ProcessLauncher::setProcessTreePriority(HANDLE hProcess, DWORD priorityClass) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return;
    setProcessTreePriorityById(GetProcessId(hProcess), priorityClass);
}

void ProcessLauncher::setProcessTreePriorityById(DWORD rootPid, DWORD priorityClass) {
    if (rootPid == 0) return;
    applyPriority(rootPid, priorityClass);
    for (DWORD childPid : collectChildProcesses(rootPid))
        applyPriority(childPid, priorityClass);
}

} // namespace chimera::instance
