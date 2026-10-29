// Apolo 0.1.0 - monitor e limpeza conservadora de memoria para Windows 11.
// Implementacao independente, utilizando APIs documentadas do Windows.

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <array>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"ApoloMainWindow";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr int kRefreshButton = 101;
constexpr int kCleanButton = 102;
constexpr UINT kRefreshIntervalMs = 2500;

HWND gRamLabel = nullptr;
HWND gCommitLabel = nullptr;
HWND gStatusLabel = nullptr;
HFONT gFont = nullptr;

struct MemorySnapshot {
    ULONGLONG total = 0;
    ULONGLONG available = 0;
    ULONGLONG commitUsed = 0;
    ULONGLONG commitLimit = 0;
    DWORD loadPercent = 0;
    bool valid = false;
};

struct CleanupResult {
    unsigned eligible = 0;
    unsigned cleaned = 0;
    unsigned skipped = 0;
    MemorySnapshot before;
    MemorySnapshot after;
};

double toGiB(ULONGLONG bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

MemorySnapshot readMemory() {
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);

    MemorySnapshot result;
    if (!GlobalMemoryStatusEx(&status)) {
        return result;
    }

    result.total = status.ullTotalPhys;
    result.available = status.ullAvailPhys;
    result.loadPercent = status.dwMemoryLoad;
    result.valid = true;

    PERFORMANCE_INFORMATION perf = {};
    perf.cb = sizeof(perf);

    if (GetPerformanceInfo(&perf, sizeof(perf))) {
        result.commitUsed =
            static_cast<ULONGLONG>(perf.CommitTotal) * perf.PageSize;
        result.commitLimit =
            static_cast<ULONGLONG>(perf.CommitLimit) * perf.PageSize;
    }

    return result;
}

void refreshDisplay() {
    const MemorySnapshot memory = readMemory();

    if (!memory.valid) {
        SetWindowTextW(gRamLabel, L"Nao foi possivel consultar a memoria.");
        SetWindowTextW(gCommitLabel, L"");
        return;
    }

    wchar_t buffer[256] = {};
    std::swprintf(
        buffer, std::size(buffer),
        L"RAM: %.2f GiB usados / %.2f GiB totais  (%lu%%)",
        toGiB(memory.total - memory.available),
        toGiB(memory.total),
        static_cast<unsigned long>(memory.loadPercent));
    SetWindowTextW(gRamLabel, buffer);

    if (memory.commitLimit != 0) {
        std::swprintf(
            buffer, std::size(buffer),
            L"RAM disponivel: %.2f GiB     Comprometida: %.2f / %.2f GiB",
            toGiB(memory.available),
            toGiB(memory.commitUsed),
            toGiB(memory.commitLimit));
    } else {
        std::swprintf(
            buffer, std::size(buffer),
            L"RAM disponivel: %.2f GiB",
            toGiB(memory.available));
    }

    SetWindowTextW(gCommitLabel, buffer);
}

bool readTokenUser(HANDLE token, std::vector<BYTE>& output, PSID& sid) {
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);

    if (needed < sizeof(TOKEN_USER) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }

    output.resize(needed);
    if (!GetTokenInformation(token, TokenUser, output.data(), needed, &needed)) {
        return false;
    }

    sid = reinterpret_cast<TOKEN_USER*>(output.data())->User.Sid;
    return sid != nullptr && IsValidSid(sid);
}

bool isProtectedShellProcess(const wchar_t* filename) {
    // Evita perturbar processos centrais do Windows e a propria interface.
    constexpr std::array<const wchar_t*, 16> excluded = {
        L"Apolo.exe", L"explorer.exe", L"dwm.exe", L"csrss.exe",
        L"winlogon.exe", L"wininit.exe", L"services.exe", L"lsass.exe",
        L"svchost.exe", L"sihost.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe",
        L"RuntimeBroker.exe", L"TextInputHost.exe", L"ApplicationFrameHost.exe"
    };

    for (const wchar_t* name : excluded) {
        if (_wcsicmp(filename, name) == 0) {
            return true;
        }
    }
    return false;
}

CleanupResult cleanCurrentUserProcesses() {
    CleanupResult result;
    result.before = readMemory();

    HANDLE currentToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &currentToken)) {
        result.after = readMemory();
        return result;
    }

    std::vector<BYTE> currentTokenBuffer;
    PSID currentSid = nullptr;
    if (!readTokenUser(currentToken, currentTokenBuffer, currentSid)) {
        CloseHandle(currentToken);
        result.after = readMemory();
        return result;
    }

    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) {
        CloseHandle(currentToken);
        result.after = readMemory();
        return result;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        CloseHandle(currentToken);
        result.after = readMemory();
        return result;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            const DWORD pid = entry.th32ProcessID;
            if (pid == 0 || pid == GetCurrentProcessId() ||
                isProtectedShellProcess(entry.szExeFile)) {
                continue;
            }

            DWORD session = 0;
            if (!ProcessIdToSessionId(pid, &session) || session != currentSession) {
                continue;
            }

            // Primeiro solicita somente leitura; privilegios de alteracao
            // sao solicitados apenas apos confirmar usuario e sessao.
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, pid);

            if (!process) {
                ++result.skipped;
                continue;
            }

            HANDLE token = nullptr;
            if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
                ++result.skipped;
                CloseHandle(process);
                continue;
            }

            std::vector<BYTE> candidateTokenBuffer;
            PSID candidateSid = nullptr;
            const bool sameUser =
                readTokenUser(token, candidateTokenBuffer, candidateSid) &&
                EqualSid(currentSid, candidateSid);
            CloseHandle(token);

            if (sameUser) {
                ++result.eligible;
                HANDLE writable = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA,
                    FALSE, pid);
                if (writable && EmptyWorkingSet(writable)) {
                    ++result.cleaned;
                } else {
                    ++result.skipped;
                }
                if (writable) {
                    CloseHandle(writable);
                }
            }

            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    CloseHandle(currentToken);
    result.after = readMemory();
    return result;
}

void applyFont(HWND control) {
    if (control && gFont) {
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(gFont), TRUE);
    }
}

HWND addControl(HWND parent, const wchar_t* klass,
                const wchar_t* caption, DWORD style,
                int x, int y, int width, int height, int id = 0) {
    HWND control = CreateWindowExW(
        0, klass, caption, style | WS_CHILD | WS_VISIBLE,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    applyFont(control);
    return control;
}

LRESULT CALLBACK mainWindowProc(HWND window, UINT message,
                                WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");

        addControl(window, L"STATIC", L"APOLO  |  Monitor de memoria",
                   SS_LEFT, 25, 23, 590, 35);
        gRamLabel = addControl(window, L"STATIC", L"Consultando...",
                               SS_LEFT, 25, 79, 590, 32);
        gCommitLabel = addControl(window, L"STATIC", L"", SS_LEFT,
                                  25, 121, 590, 32);

        addControl(window, L"BUTTON", L"Atualizar", BS_PUSHBUTTON,
                   25, 172, 145, 38, kRefreshButton);
        addControl(window, L"BUTTON", L"Limpar memoria (manual)",
                   BS_PUSHBUTTON, 183, 172, 234, 38, kCleanButton);

        gStatusLabel = addControl(
            window, L"STATIC",
            L"Sem atualizacoes automaticas, rede ou execucao elevada silenciosa.",
            SS_LEFT, 25, 232, 590, 66);

        SetTimer(window, kRefreshTimer, kRefreshIntervalMs, nullptr);
        refreshDisplay();
        return 0;
    }

    case WM_TIMER:
        if (wParam == kRefreshTimer) {
            refreshDisplay();
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kRefreshButton) {
            refreshDisplay();
        } else if (LOWORD(wParam) == kCleanButton) {
            const int response = MessageBoxW(
                window,
                L"A operacao reduz os conjuntos de trabalho de aplicativos "
                L"da sua conta e pode causar lentidao temporaria.\n\n"
                L"Deseja continuar?",
                L"Apolo - Confirmar limpeza",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

            if (response == IDYES) {
                SetWindowTextW(gStatusLabel, L"Executando limpeza...");
                const CleanupResult result = cleanCurrentUserProcesses();
                refreshDisplay();

                wchar_t summary[512] = {};
                if (result.before.valid && result.after.valid) {
                    const double change = toGiB(result.after.available) -
                                          toGiB(result.before.available);
                    std::swprintf(
                        summary, std::size(summary),
                        L"Processos tratados: %u de %u elegiveis "
                        L"(%u ignorados/falharam). "
                        L"Variacao observada de RAM disponivel: %+.2f GiB.",
                        result.cleaned, result.eligible,
                        result.skipped, change);
                } else {
                    std::swprintf(
                        summary, std::size(summary),
                        L"Processos tratados: %u de %u elegiveis "
                        L"(%u ignorados/falharam).",
                        result.cleaned, result.eligible, result.skipped);
                }
                SetWindowTextW(gStatusLabel, summary);
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(window, kRefreshTimer);
        if (gFont) {
            DeleteObject(gFont);
            gFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = mainWindowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0, kWindowClass, L"Apolo - Gerenciamento de memoria",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 655, 360,
        nullptr, nullptr, instance, nullptr);

    if (!window) {
        return 2;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
