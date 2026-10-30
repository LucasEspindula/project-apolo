#include "analysis.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <cwchar>
#include <string>
#include <vector>

namespace apolo {
namespace {

constexpr wchar_t kAnalysisClass[] = L"ApoloAnalysisWindow";
constexpr wchar_t kChartClass[] = L"ApoloHistoryChart";
constexpr std::size_t kHistoryCapacity = 360;  // 15 min a cada ~2,5s
constexpr ULONGLONG kMinSampleGapMs = 2000;
constexpr int kRangeComboId = 410;
constexpr int kSortComboId = 411;

struct Telemetry {
    ULONGLONG time = 0;
    bool cpuValid = false;
    double cpu = 0;
    bool ramValid = false;
    double ram = 0;
    double available = 0;
    bool commitValid = false;
    double commit = 0;
    bool diskValid = false;
    double diskMiB = 0;
};

std::array<Telemetry, kHistoryCapacity> gHistory{};
std::size_t gCount = 0;
std::size_t gNext = 0;
ULONGLONG gLastRecord = 0;

std::array<ProcessSample, 24> gProcesses{};
std::size_t gProcessCount = 0;
bool gProcessesValid = false;

HWND gWindow = nullptr;
HWND gGraph = nullptr;
HWND gList = nullptr;
HWND gDiagnosis = nullptr;
HWND gRange = nullptr;
HWND gSort = nullptr;
HWND gProcessHeading = nullptr;
HWND gTitleHeading = nullptr;
int gRangeIndex = 0;
int gSortIndex = 0;

ULONGLONG selectedWindowMs() {
    if (gRangeIndex == 1) return 300000;
    if (gRangeIndex == 2) return 900000;
    return 60000;
}

const Telemetry& ordered(std::size_t i) {
    return gHistory[(gNext + kHistoryCapacity - gCount + i) % kHistoryCapacity];
}

double percentage(double value) {
    return std::clamp(value, 0.0, 100.0);
}

void font(HWND child) {
    if (child) {
        SendMessageW(child, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
}

HWND label(HWND parent, const wchar_t* text, int id = 0) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_LEFT,
        0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    font(h);
    return h;
}

void addComboItem(HWND h, const wchar_t* text) {
    SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

void arrange(HWND window) {
    if (!gGraph) return;
    RECT client = {};
    GetClientRect(window, &client);
    const int w = std::max(520, client.right);
    const int h = std::max(490, client.bottom);
    const int chartHeight = std::max(210, (h - 160) / 2);
    const int headingY = 58 + chartHeight + 13;
    const int listY = headingY + 29;
    const int listHeight = std::max(95, h - listY - 87);
    MoveWindow(gTitleHeading, 15, 15, 360, 27, TRUE);
    MoveWindow(gGraph, 15, 58, w - 30, chartHeight, TRUE);
    MoveWindow(gProcessHeading, 15, headingY, 360, 25, TRUE);
    MoveWindow(gList, 15, listY, w - 30, listHeight, TRUE);
    MoveWindow(gDiagnosis, 15, h - 77, w - 30, 66, TRUE);
    MoveWindow(gRange, w - 210, 15, 191, 205, TRUE);
    MoveWindow(gSort, w - 210, headingY, 191, 180, TRUE);
    if (gList) {
        ListView_SetColumnWidth(gList, 0, std::max(180, w - 260));
        ListView_SetColumnWidth(gList, 1, 75);
        ListView_SetColumnWidth(gList, 2, 130);
    }
}

void drawChart(HDC dc, const RECT& client) {
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(dc, TRANSPARENT);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG windowMs = selectedWindowMs();
    const int left = 105;
    const int right = std::max(left + 40, w - 18);

    double maxDiskMiB = 5.0;
    for (std::size_t i = 0; i < gCount; ++i) {
        const auto& p = ordered(i);
        if (p.time <= now && now - p.time <= windowMs && p.diskValid)
            maxDiskMiB = std::max(maxDiskMiB, p.diskMiB);
    }
    maxDiskMiB = std::ceil(maxDiskMiB / 5.0) * 5.0;

    constexpr const wchar_t* names[] = {
        L"CPU (%)", L"RAM (%)", L"Disco (MiB/s)"
    };
    constexpr COLORREF colors[] = {
        RGB(36, 117, 204), RGB(49, 140, 101), RGB(178, 105, 40)
    };

    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT)));
    for (int metric = 0; metric < 3; ++metric) {
        const int top = metric * h / 3;
        const int bottom = (metric + 1) * h / 3;
        const int yTop = top + 21;
        const int yBottom = bottom - 13;
        const double maximum = metric == 2 ? maxDiskMiB : 100.0;

        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOutW(dc, 12, top + 5, names[metric], static_cast<int>(wcslen(names[metric])));

        wchar_t maxText[45] = {};
        std::swprintf(maxText, std::size(maxText), metric == 2 ? L"%.0f" : L"%.0f%%",
                      maximum);
        TextOutW(dc, 57, yTop, maxText, static_cast<int>(wcslen(maxText)));
        TextOutW(dc, 75, yBottom - 9, L"0", 1);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(224, 227, 231));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen));
        MoveToEx(dc, left, yTop, nullptr);
        LineTo(dc, right, yTop);
        MoveToEx(dc, left, yBottom, nullptr);
        LineTo(dc, right, yBottom);
        SelectObject(dc, oldPen);
        DeleteObject(gridPen);

        HPEN chartPen = CreatePen(PS_SOLID, 2, colors[metric]);
        oldPen = static_cast<HPEN>(SelectObject(dc, chartPen));
        bool connected = false, found = false;
        ULONGLONG previousTime = 0;
        for (std::size_t i = 0; i < gCount; ++i) {
            const Telemetry& p = ordered(i);
            if (p.time > now || now - p.time > windowMs) continue;
            const bool valid = metric == 0 ? p.cpuValid :
                               metric == 1 ? p.ramValid : p.diskValid;
            if (!valid) {
                connected = false;
                continue;
            }
            const double value = metric == 0 ? p.cpu :
                                 metric == 1 ? p.ram : p.diskMiB;
            const auto age = now - p.time;
            const int x = right - static_cast<int>(
                age * static_cast<ULONGLONG>(right - left) / windowMs);
            const int y = yBottom - static_cast<int>(
                std::clamp(value / maximum, 0.0, 1.0) * (yBottom - yTop));
            if (!connected || p.time - previousTime > 8000) {
                MoveToEx(dc, x, y, nullptr);
            } else {
                LineTo(dc, x, y);
            }
            found = true;
            connected = true;
            previousTime = p.time;
        }
        SelectObject(dc, oldPen);
        DeleteObject(chartPen);
        if (!found) {
            const wchar_t* message = L"Aguardando dados...";
            TextOutW(dc, left + 14, yTop + 8, message,
                     static_cast<int>(wcslen(message)));
        }
    }
    SelectObject(dc, oldFont);
}

LRESULT CALLBACK chartProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(window, &ps);
        RECT client = {};
        GetClientRect(window, &client);
        drawChart(dc, client);
        EndPaint(window, &ps);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void renderProcesses() {
    if (!gList) return;
    ListView_DeleteAllItems(gList);
    if (!gProcessesValid) return;

    std::vector<ProcessSample> processes;
    for (std::size_t i = 0; i < gProcessCount; ++i)
        processes.push_back(gProcesses[i]);

    if (gSortIndex == 1) {
        std::sort(processes.begin(), processes.end(),
                  [](const auto& a, const auto& b) {
                      const int n = _wcsicmp(a.name.c_str(), b.name.c_str());
                      return n == 0 ? a.pid < b.pid : n < 0;
                  });
    } else if (gSortIndex == 2) {
        std::sort(processes.begin(), processes.end(),
                  [](const auto& a, const auto& b) { return a.pid < b.pid; });
    }

    for (std::size_t i = 0; i < processes.size(); ++i) {
        const auto& p = processes[i];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(p.name.c_str());
        const int index = ListView_InsertItem(gList, &item);
        if (index < 0) continue;

        wchar_t pid[32] = {}, memory[64] = {};
        std::swprintf(pid, std::size(pid), L"%lu",
                      static_cast<unsigned long>(p.pid));
        std::swprintf(memory, std::size(memory), L"%.1f MiB",
                      static_cast<double>(p.workingSetBytes) / 1048576.0);
        ListView_SetItemText(gList, index, 1, pid);
        ListView_SetItemText(gList, index, 2, memory);
    }
}

LRESULT CALLBACK analysisProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gTitleHeading = label(window, L"HISTÓRICO DE RECURSOS (DADOS LOCAIS)");
        gRange = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 200, 170, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRangeComboId)),
            GetModuleHandleW(nullptr), nullptr);
        font(gRange);
        addComboItem(gRange, L"Último 1 minuto");
        addComboItem(gRange, L"Últimos 5 minutos");
        addComboItem(gRange, L"Últimos 15 minutos");
        SendMessageW(gRange, CB_SETCURSEL, gRangeIndex, 0);

        gGraph = CreateWindowExW(WS_EX_CLIENTEDGE, kChartClass, L"",
            WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        gProcessHeading = label(window, L"PROCESSOS DA SUA CONTA (ATÉ 24, POR WORKING SET)");
        gSort = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 200, 170, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortComboId)),
            GetModuleHandleW(nullptr), nullptr);
        font(gSort);
        addComboItem(gSort, L"Maior memória");
        addComboItem(gSort, L"Nome (A-Z)");
        addComboItem(gSort, L"PID crescente");
        SendMessageW(gSort, CB_SETCURSEL, gSortIndex, 0);

        INITCOMMONCONTROLSEX controls = {};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&controls);
        gList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            0, 0, 1, 1, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        font(gList);
        ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* titles[] = {L"Processo", L"PID", L"Working set"};
        for (int i = 0; i < 3; ++i) {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<LPWSTR>(titles[i]);
            column.cx = i == 0 ? 500 : i == 1 ? 75 : 125;
            column.iSubItem = i;
            ListView_InsertColumn(gList, i, &column);
        }
        gDiagnosis = label(window, L"Aguardando dados para diagnóstico...");
        arrange(window);
        renderProcesses();
        SetWindowTextW(gDiagnosis, diagnosticSummary().c_str());
        return 0;
    }
    case WM_SIZE:
        arrange(window);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            if (LOWORD(wParam) == kRangeComboId) {
                const LRESULT selection = SendMessageW(gRange, CB_GETCURSEL, 0, 0);
                if (selection >= 0 && selection <= 2) gRangeIndex = int(selection);
                InvalidateRect(gGraph, nullptr, TRUE);
            } else if (LOWORD(wParam) == kSortComboId) {
                const LRESULT selection = SendMessageW(gSort, CB_GETCURSEL, 0, 0);
                if (selection >= 0 && selection <= 2) gSortIndex = int(selection);
                renderProcesses();
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        gWindow = nullptr;
        gGraph = nullptr;
        gList = nullptr;
        gDiagnosis = nullptr;
        gRange = nullptr;
        gSort = nullptr;
        gProcessHeading = nullptr;
        gTitleHeading = nullptr;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerWindows() {
    static bool done = false;
    if (done) return true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW chart = {};
    chart.cbSize = sizeof(chart);
    chart.hInstance = instance;
    chart.lpfnWndProc = chartProc;
    chart.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    chart.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    chart.lpszClassName = kChartClass;
    if (!RegisterClassExW(&chart) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    WNDCLASSEXW analysis = {};
    analysis.cbSize = sizeof(analysis);
    analysis.hInstance = instance;
    analysis.lpfnWndProc = analysisProc;
    analysis.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    analysis.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    analysis.lpszClassName = kAnalysisClass;
    if (!RegisterClassExW(&analysis) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    done = true;
    return true;
}

} // namespace

void recordTelemetry(const PerformanceSample& performance,
                     bool ramValid, double ramUsedPercent,
                     double availablePercent,
                     bool commitValid, double commitPercent) {
    // A lista permanece a mais recente, sem armazenar nomes no histórico.
    if (performance.processesValid) {
        gProcesses = performance.topProcesses;
        gProcessCount = std::min(performance.processCount, gProcesses.size());
        gProcessesValid = true;
    }
    const ULONGLONG now = GetTickCount64();
    if (gLastRecord != 0 && now - gLastRecord < kMinSampleGapMs) return;
    gLastRecord = now;

    Telemetry point;
    point.time = now;
    point.cpuValid = performance.cpuValid;
    point.cpu = percentage(performance.cpuPercent);
    point.ramValid = ramValid;
    point.ram = percentage(ramUsedPercent);
    point.available = percentage(availablePercent);
    point.commitValid = commitValid;
    point.commit = percentage(commitPercent);
    point.diskValid = performance.diskValid;
    point.diskMiB = (performance.diskReadBytesPerSecond +
                     performance.diskWriteBytesPerSecond) / 1048576.0;
    if (!std::isfinite(point.diskMiB) || point.diskMiB < 0.0)
        point.diskValid = false;
    gHistory[gNext] = point;
    gNext = (gNext + 1) % kHistoryCapacity;
    if (gCount < kHistoryCapacity) ++gCount;
}

std::wstring diagnosticSummary() {
    const ULONGLONG now = GetTickCount64();
    unsigned totalCpu = 0, highCpu = 0;
    unsigned totalRam = 0, lowAvailable = 0;
    unsigned totalCommit = 0, highCommit = 0;
    unsigned jointPressure = 0, totalJoint = 0;

    for (std::size_t i = 0; i < gCount; ++i) {
        const Telemetry& p = ordered(i);
        if (p.time > now || now - p.time > 60000) continue;
        if (p.cpuValid) {
            ++totalCpu;
            if (p.cpu >= 90.0) ++highCpu;
        }
        if (p.ramValid) {
            ++totalRam;
            if (p.available < 10.0) ++lowAvailable;
        }
        if (p.commitValid) {
            ++totalCommit;
            if (p.commit >= 90.0) ++highCommit;
        }
        if (p.ramValid && p.commitValid) {
            ++totalJoint;
            if (p.available < 10.0 && p.commit >= 90.0) ++jointPressure;
        }
    }

    auto sustained = [](unsigned positive, unsigned samples) {
        return samples >= 8 && positive * 4 >= samples * 3;
    };
    if (sustained(jointPressure, totalJoint)) {
        return L"ATENÇÃO: RAM disponível baixa e commit elevado em pelo menos "
               L"75% das amostras recentes. Pressão de memória provável; "
               L"investigue paginação antes de qualquer limpeza.";
    }
    if (sustained(highCpu, totalCpu)) {
        return L"ATENÇÃO: CPU acima de 90% em pelo menos 75% das amostras "
               L"dos últimos 60 s. Investigue processos e tarefas em execução.";
    }
    if (sustained(lowAvailable, totalRam)) {
        return L"ATENÇÃO: RAM disponível abaixo de 10% de forma persistente. "
               L"Considere também o uso de commit e a paginação.";
    }
    if (sustained(highCommit, totalCommit)) {
        return L"ATENÇÃO: memória comprometida perto do limite em leituras "
               L"repetidas. Isso não confirma uso excessivo de RAM física.";
    }
    if (totalRam < 8 || totalCpu < 8) {
        return L"Coletando amostras (mínimo 8 leituras válidas). "
               L"Os alertas exigem repetição; nenhuma otimização é disparada.";
    }
    return L"Sem sinais persistentes de CPU alta ou pressão de memória "
           L"nesta janela de 60 s. Isto não descarta outros gargalos.";
}

void showAnalysisWindow(HWND parent) {
    if (gWindow && IsWindow(gWindow)) {
        ShowWindow(gWindow, SW_SHOWNORMAL);
        SetForegroundWindow(gWindow);
        return;
    }
    if (!registerWindows()) {
        MessageBoxW(parent, L"Não foi possível abrir o painel de análise.",
                    L"Apolo", MB_ICONERROR | MB_OK);
        return;
    }
    HWND window = CreateWindowExW(0, kAnalysisClass,
        L"Apolo v0.4 - Histórico e processos",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 935, 670,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) {
        MessageBoxW(parent, L"Erro ao criar janela de diagnóstico.",
                    L"Apolo", MB_ICONERROR | MB_OK);
        return;
    }
    gWindow = window;
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    refreshAnalysisWindow();
}

void refreshAnalysisWindow() {
    if (!gWindow || !IsWindow(gWindow)) return;
    if (gGraph) InvalidateRect(gGraph, nullptr, FALSE);
    renderProcesses();
    if (gDiagnosis) SetWindowTextW(gDiagnosis, diagnosticSummary().c_str());
}

void closeAnalysisWindow() {
    if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
}

} // namespace apolo
