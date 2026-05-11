#include "ProcessLauncher.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <array>

namespace chimera::instance {

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
                                  OutputCallback onStdout, OutputCallback onStderr) {
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

    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    BOOL created = CreateProcessW(
        nullptr,           // application name
        cmdBuf.data(),     // command line
        nullptr,           // process security attributes
        nullptr,           // thread security attributes
        redirect ? TRUE : FALSE, // inherit handles
        CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
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

} // namespace chimera::instance
