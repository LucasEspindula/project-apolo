#include "analysis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace apolo {
namespace {
constexpr std::size_t kHistoryCapacity = 360; // ~15 minutos a cada 2,5 s
constexpr ULONGLONG kMinGapMs = 2000;
std::array<HistoryPoint, kHistoryCapacity> gHistory{};
std::size_t gCount = 0, gNext = 0;
ULONGLONG gLastRecord = 0;
std::array<ProcessSample, 24> gProcesses{};
std::size_t gProcessCount = 0;

double percent(double v) {
    return std::isfinite(v) ? std::clamp(v, 0.0, 100.0) : 0.0;
}
const HistoryPoint& ordered(std::size_t i) {
    return gHistory[(gNext + kHistoryCapacity - gCount + i) % kHistoryCapacity];
}
}

void recordTelemetry(const PerformanceSample& performance,
                     bool ramValid, double ramUsedPercent,
                     double availablePercent,
                     bool commitValid, double commitPercent) {
    if (performance.processesValid) {
        gProcessCount = std::min(gProcesses.size(), performance.processCount);
        for (std::size_t i = 0; i < gProcessCount; ++i)
            gProcesses[i] = performance.topProcesses[i];
    }
    ULONGLONG now = GetTickCount64();
    if (gLastRecord != 0 && now - gLastRecord < kMinGapMs) return;
    gLastRecord = now;
    HistoryPoint p;
    p.timestamp = now;
    p.cpuValid = performance.cpuValid;
    p.cpuPercent = percent(performance.cpuPercent);
    p.ramValid = ramValid;
    p.ramPercent = percent(ramUsedPercent);
    p.availablePercent = percent(availablePercent);
    p.commitValid = commitValid;
    p.commitPercent = percent(commitPercent);
    p.diskValid = performance.diskValid;
    p.diskMiBPerSecond =
        (performance.diskReadBytesPerSecond + performance.diskWriteBytesPerSecond) /
        1048576.0;
    if (!std::isfinite(p.diskMiBPerSecond) || p.diskMiBPerSecond < 0.0)
        p.diskValid = false;
    gHistory[gNext] = p;
    gNext = (gNext + 1) % kHistoryCapacity;
    if (gCount < kHistoryCapacity) ++gCount;
}

std::size_t copyHistoryPoints(HistoryPoint* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    const auto n = std::min(gCount, capacity);
    const auto skip = gCount - n;
    for (std::size_t i = 0; i < n; ++i) output[i] = ordered(i + skip);
    return n;
}

std::size_t copyRecentProcesses(ProcessSample* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    const auto n = std::min(gProcessCount, capacity);
    for (std::size_t i = 0; i < n; ++i) output[i] = gProcesses[i];
    return n;
}

std::wstring diagnosticSummary() {
    ULONGLONG now = GetTickCount64();
    unsigned cpuCount=0, cpuHigh=0, ramCount=0, ramLow=0;
    unsigned commitCount=0, commitHigh=0, jointCount=0, jointHigh=0;
    for (std::size_t i=0;i<gCount;++i) {
        const auto& p=ordered(i);
        if (p.timestamp > now || now-p.timestamp > 60000) continue;
        if (p.cpuValid) {
            ++cpuCount;
            if (p.cpuPercent >= 90.0) ++cpuHigh;
        }
        if (p.ramValid) {
            ++ramCount;
            if (p.availablePercent < 10.0) ++ramLow;
        }
        if (p.commitValid) {
            ++commitCount;
            if (p.commitPercent >= 90.0) ++commitHigh;
        }
        if (p.ramValid && p.commitValid) {
            ++jointCount;
            if (p.availablePercent < 10.0 && p.commitPercent >= 90.0)
                ++jointHigh;
        }
    }
    auto sustained=[](unsigned yes,unsigned count) {
        return count >= 8 && yes*4 >= count*3;
    };
    if (sustained(jointHigh,jointCount))
        return L"Detectei pressão na RAM e no limite de memória comprometida. "
               L"Vou continuar monitorando; investigue a paginação antes de limpar.";
    if (sustained(cpuHigh,cpuCount))
        return L"Detectei uso de CPU acima de 90% em leituras repetidas. "
               L"Vou seguir monitorando; confira os processos ativos.";
    if (sustained(ramLow,ramCount))
        return L"Detectei pouca RAM disponível. Confira os processos que mais "
               L"consomem memória; seguirei monitorando.";
    if (sustained(commitHigh,commitCount))
        return L"Detectei memória comprometida próxima ao limite. A RAM física "
               L"pode ainda estar disponível. Continuarei monitorando.";
    if (cpuCount < 8 || ramCount < 8)
        return L"Estou reunindo dados para avaliar o sistema. Ainda não há "
               L"amostras suficientes para um diagnóstico confiável.";
    return L"Não detectei pressão persistente de CPU ou memória. Continuo "
           L"monitorando; outros gargalos ainda podem existir.";
}
} // namespace apolo
