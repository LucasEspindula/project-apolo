#pragma once

#include "performance.h"
#include <windows.h>
#include <string>

namespace apolo {

// Dados de leitura; nenhuma destas funcoes altera processos ou inicia limpeza.
void recordTelemetry(const PerformanceSample& performance,
                     bool ramValid, double ramUsedPercent,
                     double availablePercent,
                     bool commitValid, double commitPercent);
std::wstring diagnosticSummary();
void showAnalysisWindow(HWND parent);
void refreshAnalysisWindow();
void closeAnalysisWindow();

} // namespace apolo
