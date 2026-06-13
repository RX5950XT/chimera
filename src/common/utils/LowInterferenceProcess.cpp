#include "LowInterferenceProcess.h"

#include <QProcess>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace chimera::utils {

void applyLowInterferencePriority(QProcess *process) {
#ifdef _WIN32
    if (!process || process->processId() <= 0) return;
    HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                static_cast<DWORD>(process->processId()));
    if (!handle) return;

    SetPriorityClass(handle, BELOW_NORMAL_PRIORITY_CLASS);
#ifdef MEMORY_PRIORITY_LOW
    MEMORY_PRIORITY_INFORMATION memoryPriority = {};
    memoryPriority.MemoryPriority = MEMORY_PRIORITY_LOW;
    SetProcessInformation(handle, ProcessMemoryPriority,
                          &memoryPriority, sizeof(memoryPriority));
#endif
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
    PROCESS_POWER_THROTTLING_STATE throttling = {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    throttling.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttling.StateMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#endif
    SetProcessInformation(handle, ProcessPowerThrottling, &throttling, sizeof(throttling));
#endif
    CloseHandle(handle);
#else
    Q_UNUSED(process)
#endif
}

} // namespace chimera::utils
