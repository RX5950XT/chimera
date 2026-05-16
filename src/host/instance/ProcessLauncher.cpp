#include "ProcessLauncher.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <array>
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

} // namespace

ProcessLauncher::Result ProcessLauncher::runSync(const std::string &exe, const std::vector<std::string> &args) {
    Result res{ -1, {}, {} };
    std::string cmd = "\"" + exe + "\"";
    for (auto &a : args) cmd += " \"" + a + "\"";
    FILE *pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return res;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        res.stdoutText += buffer;
    }
    res.exitCode = _pclose(pipe);
    return res;
}

HANDLE ProcessLauncher::runAsync(const std::string &exe, const std::vector<std::string> &args,
                                  OutputCallback onStdout, OutputCallback onStderr,
                                  bool startHidden) {
    // Build command line
    std::wstring cmdLine = L"\"";
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, nullptr, 0);
        std::wstring wexe(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, wexe.data(), wlen);
        cmdLine += wexe;
    }
    cmdLine += L"\"";
    for (auto &a : args) {
        cmdLine += L" \"";
        int wlen = MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
        std::wstring wa(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, wa.data(), wlen);
        cmdLine += wa;
        cmdLine += L"\"";
    }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    HANDLE hStdErrRead = nullptr, hStdErrWrite = nullptr;

    bool redirect = (onStdout != nullptr) || (onStderr != nullptr);

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
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdOutWrite;
        si.hStdError = hStdErrWrite;
    }
    if (startHidden) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }

    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    BOOL created = CreateProcessW(
        nullptr,           // application name
        cmdBuf.data(),     // command line
        nullptr,           // process security attributes
        nullptr,           // thread security attributes
        redirect ? TRUE : FALSE, // inherit handles
        CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        nullptr,           // environment
        nullptr,           // current directory
        &si,
        &pi
    );

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
        // Launch background threads to read pipes
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
    if (GetExitCodeProcess(hProcess, &code)) {
        return code == STILL_ACTIVE;
    }
    return false;
}

bool ProcessLauncher::terminate(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return false;
    const DWORD rootPid = GetProcessId(hProcess);
    auto children = collectChildProcesses(rootPid);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        terminateProcessId(*it);
    }
    return TerminateProcess(hProcess, 1) == TRUE;
}

int ProcessLauncher::waitForExit(HANDLE hProcess, int timeoutMs) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return -1;
    DWORD result = WaitForSingleObject(hProcess, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
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
    for (DWORD childPid : collectChildProcesses(rootPid)) {
        applyPriority(childPid, priorityClass);
    }
}

} // namespace chimera::instance
