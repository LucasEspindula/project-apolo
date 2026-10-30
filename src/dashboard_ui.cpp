#include "dashboard_ui.h"

#include <commctrl.h>
#include <algorithm>
#include <array>
#include <iterator>
#include <string>

namespace apolo {
namespace {

HWND makeControl(HWND parent, DashboardUi& ui, bool settings,
                 const wchar_t* className, const wchar_t* caption,
                 DWORD style, int id = 0, DWORD extendedStyle = 0) {
    HWND result = CreateWindowExW(
        extendedStyle, className, caption,
        WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, parent,
        id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (result) {
        SendMessageW(result, WM_SETFONT,
            reinterpret_cast<WPARAM>(ui.normalFont), TRUE);
        (settings ? ui.settingsControls : ui.dashboardControls).push_back(result);
    }
    return result;
}

void place(HWND control, int x, int y, int width, int height) {
    if (control) MoveWindow(control, x, y,
                            std::max(1, width), std::max(1, height), TRUE);
}

void fillCombo(HWND combo, const int* values, size_t count,
               int chosen, const wchar_t* unit) {
    if (!combo) return;
    for (size_t i = 0; i < count; ++i) {
        const std::wstring item = std::to_wstring(values[i]) + unit;
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item.c_str()));
        if (values[i] == chosen)
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
    }
}

} // namespace

bool createDashboardUi(HWND parent, DashboardUi& ui, DashboardCommands ids,
                       bool policyLocked, bool autoEnabled,
                       bool startHidden, int threshold, int interval) {
    INITCOMMONCONTROLSEX classes = {};
    classes.dwSize = sizeof(classes);
    classes.dwICC = ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&classes)) return false;

    ui.normalFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    ui.headerFont = CreateFontW(-33, 0, 0, 0, FW_BOLD, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    if (!ui.normalFont || !ui.headerFont) return false;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    ui.header = CreateWindowExW(0, L"STATIC", L"APOLO",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 1, 1, parent, nullptr, instance, nullptr);
    ui.subtitle = CreateWindowExW(0, L"STATIC",
        L"Centro de desempenho  |  v0.5.0 (piloto)",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 1, 1, parent, nullptr, instance, nullptr);
    ui.tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
        0, 0, 1, 1, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabControlId)),
        instance, nullptr);
    ui.status = CreateWindowExW(0, L"STATIC", L"Apolo pronto. Monitoramento local.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 1, 1, parent, nullptr, instance, nullptr);
    if (!ui.header || !ui.subtitle || !ui.tabs || !ui.status) return false;
    SendMessageW(ui.header, WM_SETFONT,
                 reinterpret_cast<WPARAM>(ui.headerFont), TRUE);
    SendMessageW(ui.subtitle, WM_SETFONT,
                 reinterpret_cast<WPARAM>(ui.normalFont), TRUE);
    SendMessageW(ui.tabs, WM_SETFONT,
                 reinterpret_cast<WPARAM>(ui.normalFont), TRUE);
    SendMessageW(ui.status, WM_SETFONT,
                 reinterpret_cast<WPARAM>(ui.normalFont), TRUE);

    TCITEMW tabItem = {};
    tabItem.mask = TCIF_TEXT;
    tabItem.pszText = const_cast<LPWSTR>(L"Visão geral");
    TabCtrl_InsertItem(ui.tabs, 0, &tabItem);
    tabItem.pszText = const_cast<LPWSTR>(L"Configurações");
    TabCtrl_InsertItem(ui.tabs, 1, &tabItem);

    const auto control = [&](bool settings, const wchar_t* klass,
                             const wchar_t* name, DWORD style, int id = 0) {
        return makeControl(parent, ui, settings, klass, name, style, id);
    };
    // Aba Visão geral: indicadores, diagnóstico e ações.
    ui.ramLabel = control(false, L"STATIC", L"RAM: coletando...", SS_LEFT);
    ui.cpuLabel = control(false, L"STATIC", L"CPU: coletando...", SS_LEFT);
    ui.ramProgress = control(false, PROGRESS_CLASSW, L"", PBS_SMOOTH);
    ui.cpuProgress = control(false, PROGRESS_CLASSW, L"", PBS_SMOOTH);
    ui.commitLabel = control(false, L"STATIC", L"Memória comprometida: --", SS_LEFT);
    ui.diskLabel = control(false, L"STATIC", L"Disco: coletando...", SS_LEFT);
    ui.diagnosisLabel = control(false, L"STATIC", L"Aguardando diagnóstico...", SS_LEFT);
    ui.refreshButton = control(false, L"BUTTON", L"Atualizar", BS_PUSHBUTTON | WS_TABSTOP, ids.refresh);
    ui.cleanButton = control(false, L"BUTTON", L"Limpar memória...", BS_PUSHBUTTON | WS_TABSTOP, ids.clean);
    ui.analysisButton = control(false, L"BUTTON", L"Gráficos e análise", BS_PUSHBUTTON | WS_TABSTOP, ids.analysis);
    ui.historyButton = control(false, L"BUTTON", L"Histórico", BS_PUSHBUTTON | WS_TABSTOP, ids.history);
    ui.processTitle = control(false, L"STATIC",
        L"PROCESSOS COM MAIOR MEMÓRIA RESIDENTE (SUA CONTA)", SS_LEFT);
    for (size_t i = 0; i < ui.processLabels.size(); ++i) {
        ui.processLabels[i] = control(false, L"STATIC",
            i == 0 ? L"Consultando processos..." : L"", SS_LEFT);
    }

    // Aba Configurações: as regras de negócio existentes permanecem intactas.
    ui.settingsTitle = control(true, L"STATIC",
        L"CONFIGURAÇÕES DO APOLO", SS_LEFT);
    ui.autoToggle = control(true, L"BUTTON", L"Ativar limpeza automática",
        BS_AUTOCHECKBOX | WS_TABSTOP, ids.autoClean);
    ui.hiddenToggle = control(true, L"BUTTON", L"Abrir diretamente na bandeja",
        BS_AUTOCHECKBOX | WS_TABSTOP, ids.startHidden);
    ui.thresholdLabel = control(true, L"STATIC", L"Limiar de uso da RAM", SS_LEFT);
    ui.thresholdCombo = control(true, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ids.threshold);
    ui.intervalLabel = control(true, L"STATIC", L"Intervalo mínimo entre limpezas", SS_LEFT);
    ui.intervalCombo = control(true, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ids.interval);
    ui.minutesLabel = control(true, L"STATIC", L"minutos", SS_LEFT);
    ui.saveButton = control(true, L"BUTTON", L"Salvar configurações",
        BS_PUSHBUTTON | WS_TABSTOP, ids.save);
    ui.policyMessage = control(true, L"STATIC",
        policyLocked
        ? L"Política da TI ativa. Configurações bloqueadas neste computador."
        : L"Configurações individuais salvas no perfil do Windows.",
        SS_LEFT);
    ui.safetyMessage = control(true, L"STATIC",
        L"A limpeza automática é opcional e pode causar lentidão. "
        L"Ela permanece desligada por padrão. "
        L"Abrir na bandeja não significa iniciar com o Windows.",
        SS_LEFT);

    // Falhar cedo se os controles essenciais não forem criados.
    if (!ui.ramLabel || !ui.cpuLabel || !ui.ramProgress || !ui.cpuProgress ||
        !ui.commitLabel || !ui.diskLabel || !ui.diagnosisLabel ||
        !ui.refreshButton || !ui.cleanButton || !ui.analysisButton ||
        !ui.historyButton || !ui.processTitle ||
        !ui.settingsTitle || !ui.autoToggle || !ui.hiddenToggle ||
        !ui.thresholdLabel || !ui.thresholdCombo || !ui.intervalLabel ||
        !ui.intervalCombo || !ui.minutesLabel || !ui.saveButton ||
        !ui.policyMessage || !ui.safetyMessage) {
        return false;
    }
    for (HWND row : ui.processLabels)
        if (!row) return false;

    SendMessageW(ui.ramProgress, PBM_SETRANGE32, 0, 100);
    SendMessageW(ui.cpuProgress, PBM_SETRANGE32, 0, 100);
    constexpr std::array<int, 6> limits = {70, 75, 80, 85, 90, 95};
    constexpr std::array<int, 5> intervals = {15, 30, 60, 120, 240};
    fillCombo(ui.thresholdCombo, limits.data(), limits.size(), threshold, L"%");
    fillCombo(ui.intervalCombo, intervals.data(), intervals.size(), interval, L"");
    SendMessageW(ui.autoToggle, BM_SETCHECK,
                 autoEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ui.hiddenToggle, BM_SETCHECK,
                 startHidden ? BST_CHECKED : BST_UNCHECKED, 0);
    if (policyLocked) {
        EnableWindow(ui.autoToggle, FALSE);
        EnableWindow(ui.hiddenToggle, FALSE);
        EnableWindow(ui.thresholdCombo, FALSE);
        EnableWindow(ui.intervalCombo, FALSE);
        EnableWindow(ui.saveButton, FALSE);
    }
    layoutDashboardUi(parent, ui);
    setDashboardTab(ui, 0);
    return true;
}

void layoutDashboardUi(HWND parent, DashboardUi& ui) {
    if (!ui.tabs) return;
    RECT rect = {};
    GetClientRect(parent, &rect);
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    if (width < 200 || height < 200) return;

    const int margin = 25;
    const int usable = width - 2 * margin;
    place(ui.header, margin + 2, 14, usable, 45);
    place(ui.subtitle, margin + 3, 61, usable, 27);
    place(ui.tabs, margin, 94, usable, std::max(300, height - 167));
    place(ui.status, margin + 2, height - 57, usable, 47);

    const int innerX = margin + 27;
    const int innerWidth = usable - 54;
    const int gap = 26;
    const int half = (innerWidth - gap) / 2;
    const int rightX = innerX + half + gap;

    place(ui.ramLabel, innerX, 156, half, 30);
    place(ui.cpuLabel, rightX, 156, half, 30);
    place(ui.ramProgress, innerX, 191, half, 18);
    place(ui.cpuProgress, rightX, 191, half, 18);
    place(ui.commitLabel, innerX, 230, half, 55);
    place(ui.diskLabel, rightX, 230, half, 55);
    place(ui.diagnosisLabel, innerX, 302, innerWidth, 62);

    const int actionGap = 12;
    const int buttonWidth = (innerWidth - 3 * actionGap) / 4;
    place(ui.refreshButton, innerX, 384, buttonWidth, 41);
    place(ui.cleanButton, innerX + (buttonWidth + actionGap), 384, buttonWidth, 41);
    place(ui.analysisButton, innerX + 2 * (buttonWidth + actionGap),
          384, buttonWidth, 41);
    place(ui.historyButton, innerX + 3 * (buttonWidth + actionGap),
          384, buttonWidth, 41);

    place(ui.processTitle, innerX, 454, innerWidth, 29);
    for (size_t i = 0; i < ui.processLabels.size(); ++i)
        place(ui.processLabels[i], innerX, 493 + static_cast<int>(i) * 31,
              innerWidth, 28);

    place(ui.settingsTitle, innerX, 160, innerWidth, 33);
    place(ui.autoToggle, innerX, 220, half, 33);
    place(ui.hiddenToggle, innerX, 268, half + 90, 33);
    place(ui.thresholdLabel, innerX, 328, half, 28);
    place(ui.intervalLabel, rightX, 328, half, 28);
    place(ui.thresholdCombo, innerX, 366, half - 45, 220);
    place(ui.intervalCombo, rightX, 366, half - 80, 220);
    place(ui.minutesLabel, rightX + half - 75, 372, 75, 27);
    place(ui.saveButton, innerX, 421, std::min(innerWidth, 260), 43);
    place(ui.policyMessage, innerX, 490, innerWidth, 37);
    place(ui.safetyMessage, innerX, 535, innerWidth, 65);
}

void setDashboardTab(DashboardUi& ui, int tab) {
    if (tab < 0 || tab > 1) return;
    ui.activeTab = tab;
    if (ui.tabs) TabCtrl_SetCurSel(ui.tabs, tab);
    for (HWND control : ui.dashboardControls)
        if (control) ShowWindow(control, tab == 0 ? SW_SHOW : SW_HIDE);
    for (HWND control : ui.settingsControls)
        if (control) ShowWindow(control, tab == 1 ? SW_SHOW : SW_HIDE);
}

void updateDashboardGauge(HWND gauge, unsigned percent) {
    if (gauge) SendMessageW(gauge, PBM_SETPOS,
                           static_cast<WPARAM>(std::min(percent, 100U)), 0);
}

void destroyDashboardUi(DashboardUi& ui) {
    // Janelas filhas são destruídas pelo Windows junto com o pai.
    if (ui.headerFont) DeleteObject(ui.headerFont);
    if (ui.normalFont) DeleteObject(ui.normalFont);
    ui.headerFont = nullptr;
    ui.normalFont = nullptr;
    ui.dashboardControls.clear();
    ui.settingsControls.clear();
}

} // namespace apolo
