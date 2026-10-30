#pragma once
#include "performance.h"
#include <windows.h>
#include <cstddef>
#include <string>

namespace apolo {
struct HistoryPoint {
    ULONGLONG timestamp = 0;
    bool cpuValid = false;
    double cpuPercent = 0.0;
    bool ramValid = false;
    double ramPercent = 0.0;
    double availablePercent = 0.0;
    bool commitValid = false;
    double commitPercent = 0.0;
    bool diskValid = false;
    double diskMiBPerSecond = 0.0;
};

// Leitura e armazenamento apenas na memoria do processo; sem rede.
void recordTelemetry(const PerformanceSample& performance,
                     bool ramValid, double ramUsedPercent,
                     double availablePercent,
                     bool commitValid, double commitPercent);
std::wstring diagnosticSummary();
std::size_t copyHistoryPoints(HistoryPoint* output, std::size_t capacity);
std::size_t copyRecentProcesses(ProcessSample* output, std::size_t capacity);
} // namespace apolo
