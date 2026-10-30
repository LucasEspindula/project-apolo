#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <string>

namespace apolo {

struct ProcessSample {
    std::wstring name;
    DWORD pid = 0;
    unsigned long long workingSetBytes = 0;
};

struct PerformanceSample {
    bool cpuValid = false;
    double cpuPercent = 0.0;

    bool diskValid = false;
    double diskReadBytesPerSecond = 0.0;
    double diskWriteBytesPerSecond = 0.0;

    bool processesValid = false;
    std::array<ProcessSample, 24> topProcesses{};
    std::size_t processCount = 0;
};

// Apenas leitura; sem telemetria, sem alteracao de processos.
void initializePerformance();
PerformanceSample samplePerformance(bool updateProcesses);
void shutdownPerformance();

} // namespace apolo
