#include "performance.h"

#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace apolo {
namespace {

FILETIME gPreviousIdle = {};
FILETIME gPreviousKernel = {};
FILETIME gPreviousUser = {};
bool gHavePreviousCpu = false;

PDH_HQUERY gDiskQuery = nullptr;
PDH_HCOUNTER gReadCounter = nullptr;
PDH_HCOUNTER gWriteCounter = nullptr;

std::array<ProcessSample, 24> gCachedProcesses{};
std::size_t gCachedProcessCount = 0;
bool gHaveProcessSample = false;

std::uint64_t ticks(const FILETIME& time) {
    ULARGE_INTEGER value = {};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

void updateCpu(PerformanceSample& sample) {
    FILETIME idle = {}, kernel = {}, user = {};
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        gHavePreviousCpu = false;
        return;
    }

    if (gHavePreviousCpu) {
        const auto oldIdle = ticks(gPreviousIdle);
        const auto oldKernel = ticks(gPreviousKernel);
        const auto oldUser = ticks(gPreviousUser);
        const auto newIdle = ticks(idle);
        const auto newKernel = ticks(kernel);
        const auto newUser = ticks(user);

        if (newIdle >= oldIdle && newKernel >= oldKernel && newUser >= oldUser) {
            const std::uint64_t deltaIdle = newIdle - oldIdle;
            // Kernel inclui tempo ocioso, conforme documentacao GetSystemTimes.
            const std::uint64_t deltaTotal = (newKernel - oldKernel) +
                                             (newUser - oldUser);
            if (deltaTotal > 0 && deltaTotal >= deltaIdle) {
                sample.cpuValid = true;
                sample.cpuPercent = std::clamp(
                    100.0 * static_cast<double>(deltaTotal - deltaIdle) /
                    static_cast<double>(deltaTotal), 0.0, 100.0);
            }
        }
    }

    gPreviousIdle = idle;
    gPreviousKernel = kernel;
    gPreviousUser = user;
    gHavePreviousCpu = true;
}

bool counterValue(PDH_HCOUNTER handle, double& output) {
    PDH_FMT_COUNTERVALUE value = {};
    if (PdhGetFormattedCounterValue(handle, PDH_FMT_DOUBLE, nullptr, &value)
        != ERROR_SUCCESS) {
        return false;
    }
    if (value.CStatus != PDH_CSTATUS_VALID_DATA &&
        value.CStatus != PDH_CSTATUS_NEW_DATA) {
        return false;
    }
    if (!std::isfinite(value.doubleValue) || value.doubleValue < 0.0) {
        return false;
    }
    output = value.doubleValue;
    return true;
}

void updateDisk(PerformanceSample& sample) {
    if (!gDiskQuery || PdhCollectQueryData(gDiskQuery) != ERROR_SUCCESS) {
        return;
    }
    double read = 0.0, write = 0.0;
    if (counterValue(gReadCounter, read) && counterValue(gWriteCounter, write)) {
        sample.diskReadBytesPerSecond = read;
        sample.diskWriteBytesPerSecond = write;
        sample.diskValid = true;
    }
}

bool tokenUser(HANDLE process, std::vector<BYTE>& bytes) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    const bool sized = needed >= sizeof(TOKEN_USER) &&
                       GetLastError() == ERROR_INSUFFICIENT_BUFFER;
    if (!sized) {
        CloseHandle(token);
        return false;
    }

    bytes.resize(needed);
    const bool ok = GetTokenInformation(
        token, TokenUser, bytes.data(), needed, &needed) != FALSE;
    CloseHandle(token);
    if (!ok) return false;

    const auto* info = reinterpret_cast<const TOKEN_USER*>(bytes.data());
    return info->User.Sid != nullptr && IsValidSid(info->User.Sid);
}

PSID sidOf(std::vector<BYTE>& token) {
    return reinterpret_cast<TOKEN_USER*>(token.data())->User.Sid;
}

void updateProcesses() {
    gCachedProcessCount = 0;
    gHaveProcessSample = false;

    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) {
        return;
    }

    std::vector<BYTE> ownToken;
    if (!tokenUser(GetCurrentProcess(), ownToken)) return;
    PSID ownSid = sidOf(ownToken);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    std::vector<ProcessSample> candidates;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0) continue;

            DWORD session = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &session) ||
                session != currentSession) {
                continue;
            }

            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;

            std::vector<BYTE> userToken;
            const bool ownUser = tokenUser(process, userToken) &&
                                 EqualSid(ownSid, sidOf(userToken)) != FALSE;

            if (ownUser) {
                PROCESS_MEMORY_COUNTERS counters = {};
                counters.cb = sizeof(counters);
                if (GetProcessMemoryInfo(process, &counters, sizeof(counters))) {
                    candidates.push_back({
                        entry.szExeFile,
                        entry.th32ProcessID,
                        static_cast<unsigned long long>(counters.WorkingSetSize)
                    });
                }
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::sort(candidates.begin(), candidates.end(),
              [](const ProcessSample& a, const ProcessSample& b) {
                  if (a.workingSetBytes != b.workingSetBytes)
                      return a.workingSetBytes > b.workingSetBytes;
                  return a.pid < b.pid;
              });

    gCachedProcessCount = std::min(gCachedProcesses.size(), candidates.size());
    for (std::size_t i = 0; i < gCachedProcessCount; ++i) {
        gCachedProcesses[i] = candidates[i];
    }
    gHaveProcessSample = true;
}

} // namespace

void initializePerformance() {
    gHavePreviousCpu = false;
    gHaveProcessSample = false;

    // Primeira amostra da CPU: a porcentagem fica disponivel no proximo ciclo.
    PerformanceSample initial = {};
    updateCpu(initial);

    if (PdhOpenQueryW(nullptr, 0, &gDiskQuery) != ERROR_SUCCESS) {
        gDiskQuery = nullptr;
        return;
    }

    // PdhAddEnglishCounterW evita depender do idioma do Windows.
    if (PdhAddEnglishCounterW(
            gDiskQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
            0, &gReadCounter) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(
            gDiskQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
            0, &gWriteCounter) != ERROR_SUCCESS ||
        PdhCollectQueryData(gDiskQuery) != ERROR_SUCCESS) {
        PdhCloseQuery(gDiskQuery);
        gDiskQuery = nullptr;
        gReadCounter = nullptr;
        gWriteCounter = nullptr;
    }
}

PerformanceSample samplePerformance(bool updateProcessList) {
    PerformanceSample sample;
    updateCpu(sample);
    updateDisk(sample);

    if (updateProcessList) updateProcesses();
    sample.processesValid = gHaveProcessSample;
    sample.processCount = gCachedProcessCount;
    sample.topProcesses = gCachedProcesses;
    return sample;
}

void shutdownPerformance() {
    if (gDiskQuery) {
        PdhCloseQuery(gDiskQuery);
        gDiskQuery = nullptr;
        gReadCounter = nullptr;
        gWriteCounter = nullptr;
    }
}

} // namespace apolo
