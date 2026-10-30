#pragma once

#include <windows.h>
#include <array>
#include <vector>

namespace apolo {

constexpr int kTabControlId = 720;

struct DashboardCommands {
    int refresh = 0;
    int clean = 0;
    int history = 0;
    int analysis = 0;
    int autoClean = 0;
    int startHidden = 0;
    int threshold = 0;
    int interval = 0;
    int save = 0;
};

struct DashboardUi {
    HWND header = nullptr;
    HWND subtitle = nullptr;
    HWND tabs = nullptr;
    HWND ramLabel = nullptr;
    HWND commitLabel = nullptr;
    HWND cpuLabel = nullptr;
    HWND diskLabel = nullptr;
    HWND diagnosisLabel = nullptr;
    HWND processTitle = nullptr;
    std::array<HWND, 3> processLabels{};
    HWND ramProgress = nullptr;
    HWND cpuProgress = nullptr;

    HWND refreshButton = nullptr;
    HWND cleanButton = nullptr;
    HWND historyButton = nullptr;
    HWND analysisButton = nullptr;

    HWND settingsTitle = nullptr;
    HWND autoToggle = nullptr;
    HWND hiddenToggle = nullptr;
    HWND thresholdLabel = nullptr;
    HWND thresholdCombo = nullptr;
    HWND intervalLabel = nullptr;
    HWND intervalCombo = nullptr;
    HWND minutesLabel = nullptr;
    HWND saveButton = nullptr;
    HWND policyMessage = nullptr;
    HWND safetyMessage = nullptr;

    HWND status = nullptr;
    HFONT normalFont = nullptr;
    HFONT headerFont = nullptr;
    int activeTab = 0;
    std::vector<HWND> dashboardControls;
    std::vector<HWND> settingsControls;
};

bool createDashboardUi(HWND parent, DashboardUi& ui, DashboardCommands ids,
                       bool policyLocked, bool autoEnabled,
                       bool startHidden, int threshold, int interval);
void layoutDashboardUi(HWND parent, DashboardUi& ui);
void setDashboardTab(DashboardUi& ui, int tab);
void updateDashboardGauge(HWND gauge, unsigned percent);
void destroyDashboardUi(DashboardUi& ui);

} // namespace apolo
