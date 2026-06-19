#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <windows.h>
#include "ProcessLauncher.h"

using namespace chimera::instance;

static std::string echoExe() {
    return (QCoreApplication::applicationDirPath() + "/echo_args.exe").toStdString();
}

class TestProcessLauncher : public QObject {
    Q_OBJECT

private slots:
    // --- quoteArg unit tests (cases 1-3 from plan) ---

    void quoteArgPlainWord() {
        QCOMPARE(ProcessLauncher::quoteArg(L"hello"), std::wstring(L"hello"));
    }

    void quoteArgSpaces() {
        // arg with spaces must be wrapped in quotes
        QCOMPARE(ProcessLauncher::quoteArg(L"hello world"), std::wstring(L"\"hello world\""));
    }

    void quoteArgEmbeddedQuote() {
        // hello"world → "hello\"world"  (CommandLineToArgvW rule: backslash-escape the quote)
        QCOMPARE(ProcessLauncher::quoteArg(L"hello\"world"), std::wstring(L"\"hello\\\"world\""));
    }

    void quoteArgTrailingBackslash() {
        // Plain path with trailing \ — no spaces/quotes, so no quoting is applied
        QCOMPARE(ProcessLauncher::quoteArg(L"C:\\path\\"), std::wstring(L"C:\\path\\"));

        // Path WITH spaces AND trailing \: naive "C:\my path\" leaves an unmatched quote
        // because \" == escaped "; correct output doubles the trailing backslash: "C:\my path\\"
        QCOMPARE(ProcessLauncher::quoteArg(L"C:\\my path\\"), std::wstring(L"\"C:\\my path\\\\\""));
    }

    // --- utf8ToWide unit test (case 4: Unicode/Chinese path) ---

    void utf8ToWideRoundTrip() {
        // "你好世界" in UTF-8 (raw bytes, avoid char8_t issues)
        const std::string utf8 = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c";
        const std::wstring wide = ProcessLauncher::utf8ToWide(utf8);
        QCOMPARE(wide.size(), static_cast<size_t>(4));  // 4 CJK codepoints

        // Convert back to UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        QVERIFY(len > 0);
        std::string back(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, back.data(), len, nullptr, nullptr);
        QCOMPARE(back, utf8);
    }

    // --- Integration: metacharacter isolation (case 5) ---

    void runSyncMetacharactersIsolated() {
        // & ^ % ! are NOT cmd.exe metacharacters when passed via CreateProcessW (no shell)
        // echo_args receives the arg literally
        auto res = ProcessLauncher::runSync(echoExe(), {"foo&bar^%!"});
        QCOMPARE(res.exitCode, 0);
        QVERIFY(res.stdoutText.find("foo&bar^%!") != std::string::npos);
    }

    // --- Integration: pipe-buffer / deadlock (cases 6-7) ---

    void runSyncLargeStdout() {
        // ~600 KB of stdout — must not deadlock with the new impl
        auto res = ProcessLauncher::runSync(
            "powershell.exe",
            {"-NonInteractive", "-NoProfile", "-Command",
             "$x = 'A' * 1000; 1..600 | ForEach-Object { Write-Host $x }"});
        QCOMPARE(res.exitCode, 0);
        QVERIFY(res.stdoutText.size() > 400000u);
    }

    void runSyncLargeStderr() {
        // ~200 KB written to stderr — must be captured in res.stderrText
        // FAILS with _popen impl (stderrText always empty there)
        auto res = ProcessLauncher::runSync(
            "powershell.exe",
            {"-NonInteractive", "-NoProfile", "-Command",
             "$x = 'E' * 1000; 1..200 | ForEach-Object { [Console]::Error.Write($x) }"});
        QCOMPARE(res.exitCode, 0);
        QVERIFY(res.stderrText.size() > 100000u);
    }

    // --- Integration: missing executable (case 8) ---

    void runSyncMissingExe() {
        auto res = ProcessLauncher::runSync("this_binary_does_not_exist_xyz123.exe", {});
        QVERIFY(res.exitCode != 0);
    }

    // --- Integration: exit code round-trip (case 9) ---

    void runSyncExitCode() {
        auto res = ProcessLauncher::runSync("cmd.exe", {"/c", "exit 42"});
        QCOMPARE(res.exitCode, 42);
    }

    // --- Integration: embedded quote and trailing backslash (full round-trip) ---

    void runSyncArgWithEmbeddedQuote() {
        // Naive quoting "hello"world" breaks; proper quoteArg produces "hello\"world"
        auto res = ProcessLauncher::runSync(echoExe(), {"hello\"world"});
        QCOMPARE(res.exitCode, 0);
        QCOMPARE(res.stdoutText, std::string("hello\"world"));
    }

    void runSyncArgWithTrailingBackslash() {
        // Naive quoting "C:\path\" leaves unmatched quote; proper quoting doubles trailing backslash
        auto res = ProcessLauncher::runSync(echoExe(), {"C:\\path\\"});
        QCOMPARE(res.exitCode, 0);
        QCOMPARE(res.stdoutText, std::string("C:\\path\\"));
    }

    void runSyncUnicodeArg() {
        // UTF-8 arg must survive utf8ToWide conversion and arrive intact
        const std::string chinese = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c";
        auto res = ProcessLauncher::runSync(echoExe(), {chinese});
        QCOMPARE(res.exitCode, 0);
        QVERIFY(res.stdoutText.find(chinese) != std::string::npos);
    }

    void runAsyncAppliesInitialPriority() {
        HANDLE process = ProcessLauncher::runAsync(
            "powershell.exe",
            {"-NonInteractive", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
            nullptr,
            nullptr,
            true,
            BELOW_NORMAL_PRIORITY_CLASS);
        QVERIFY(process != nullptr);
        QCOMPARE(GetPriorityClass(process), static_cast<DWORD>(BELOW_NORMAL_PRIORITY_CLASS));
#ifdef MEMORY_PRIORITY_LOW
        MEMORY_PRIORITY_INFORMATION memoryPriority = {};
        QVERIFY(GetProcessInformation(process, ProcessMemoryPriority, &memoryPriority, sizeof(memoryPriority)));
        QCOMPARE(memoryPriority.MemoryPriority, static_cast<ULONG>(MEMORY_PRIORITY_LOW));
#endif
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
        PROCESS_POWER_THROTTLING_STATE throttling = {};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        QVERIFY(GetProcessInformation(process, ProcessPowerThrottling, &throttling, sizeof(throttling)));
        QVERIFY((throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) == 0);
#endif
        ProcessLauncher::terminate(process);
        QVERIFY(ProcessLauncher::waitForExit(process, 3000) >= 0);
    }

    void runAsyncAppliesIdlePriority() {
        HANDLE process = ProcessLauncher::runAsync(
            "powershell.exe",
            {"-NonInteractive", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
            nullptr,
            nullptr,
            true,
            IDLE_PRIORITY_CLASS);
        QVERIFY(process != nullptr);
        QCOMPARE(GetPriorityClass(process), static_cast<DWORD>(IDLE_PRIORITY_CLASS));
#ifdef MEMORY_PRIORITY_LOW
        MEMORY_PRIORITY_INFORMATION memoryPriority = {};
        QVERIFY(GetProcessInformation(process, ProcessMemoryPriority, &memoryPriority, sizeof(memoryPriority)));
        QCOMPARE(memoryPriority.MemoryPriority, static_cast<ULONG>(MEMORY_PRIORITY_LOW));
#endif
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
        PROCESS_POWER_THROTTLING_STATE throttling = {};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        QVERIFY(GetProcessInformation(process, ProcessPowerThrottling, &throttling, sizeof(throttling)));
        QVERIFY((throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0);
#endif
        ProcessLauncher::terminate(process);
        QVERIFY(ProcessLauncher::waitForExit(process, 3000) >= 0);
    }

    void runAsyncCapsHighPriorityToNormal() {
        HANDLE process = ProcessLauncher::runAsync(
            "powershell.exe",
            {"-NonInteractive", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
            nullptr,
            nullptr,
            true,
            HIGH_PRIORITY_CLASS);
        QVERIFY(process != nullptr);
        QCOMPARE(GetPriorityClass(process), static_cast<DWORD>(NORMAL_PRIORITY_CLASS));
        ProcessLauncher::terminate(process);
        QVERIFY(ProcessLauncher::waitForExit(process, 3000) >= 0);
    }

    void runAsyncHiddenDoesNotExposeWindow() {
        HANDLE process = ProcessLauncher::runAsync(
            "powershell.exe",
            {"-NonInteractive", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
            nullptr,
            nullptr,
            true,
            BELOW_NORMAL_PRIORITY_CLASS);
        QVERIFY(process != nullptr);
        const DWORD pid = GetProcessId(process);
        QTest::qWait(500);
        QVERIFY(ProcessLauncher::visibleWindowTitlesInProcessTreeById(pid).empty());
        QVERIFY(ProcessLauncher::terminateProcessTreeById(pid));
        QVERIFY(ProcessLauncher::waitForExit(process, 3000) >= 0);
    }
};

QTEST_MAIN(TestProcessLauncher)
#include "test_process_launcher.moc"
