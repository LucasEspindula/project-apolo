// Apolo 0.3.0 - monitoramento de desempenho e limpeza conservadora para Windows 11.
// Implementacao independente, utilizando APIs documentadas do Windows.

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>

#include "performance.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
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
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kHistoryButton = 103, kAuto = 104, kThreshold = 105, kInterval = 106;
constexpr int kStartHidden = 107, kSave = 108;
constexpr int kOpenMenu = 301, kCleanMenu = 302, kHistoryMenu = 303, kExitMenu = 304;
constexpr std::array<int, 6> kLimits = {70, 75, 80, 85, 90, 95};
constexpr std::array<int, 5> kIntervals = {15, 30, 60, 120, 240};
struct Options {
    bool automatic = false;
    int threshold = 85;
    int cooldown = 60;
    bool startHidden = false;
};
Options gOptions;
HWND gAutomatic = nullptr, gThreshold = nullptr, gInterval = nullptr;
HWND gStartHidden = nullptr, gSaveButton = nullptr;
HFONT gTitleFont = nullptr;
HWND gCpuLabel = nullptr, gDiskLabel = nullptr, gDiagnosisLabel = nullptr;
std::array<HWND, 3> gProcessLabels{};
unsigned gPerformanceTicks = 0;
bool gLocked = false, gTray = false, gExiting = false;
ULONGLONG gLastAttempt = 0;
UINT gTaskbarCreated = 0;
std::wstring gConfig, gLog, gPolicyFile;

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

MemorySnapshot gLastSnapshot;

struct CleanupResult {
    unsigned eligible = 0;
    unsigned cleaned = 0;
    unsigned skipped = 0;
    MemorySnapshot before;
    MemorySnapshot after;
};

bool exists(const std::wstring& path) {
    DWORD attrs = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring knownPath(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) return L"";
    std::wstring result(p);
    CoTaskMemFree(p);
    return result;
}
template<size_t N> int closest(int value, const std::array<int,N>& a) {
    int result = a[0];
    for (int option : a)
        if (std::abs(option-value) < std::abs(result-value)) result = option;
    return result;
}
void loadConfig() {
    std::wstring user = knownPath(FOLDERID_LocalAppData);
    if (!user.empty()) {
        std::wstring dir = user + L"\\Apolo";
        CreateDirectoryW(dir.c_str(), nullptr);
        gConfig = dir + L"\\settings.ini";
        gLog = dir + L"\\history.csv";
    }
    std::wstring shared = knownPath(FOLDERID_ProgramData);
    if (!shared.empty()) {
        gPolicyFile = shared + L"\\Apolo\\policy.ini";
        gLocked = exists(gPolicyFile);
    }
    const auto read = [](const std::wstring& path) {
        if (!exists(path)) return;
        gOptions.automatic = GetPrivateProfileIntW(L"Apolo", L"AutoCleanEnabled",
            gOptions.automatic ? 1 : 0, path.c_str()) != 0;
        gOptions.threshold = GetPrivateProfileIntW(L"Apolo", L"AutoCleanThreshold",
            gOptions.threshold, path.c_str());
        gOptions.cooldown = GetPrivateProfileIntW(L"Apolo", L"AutoCleanCooldownMinutes",
            gOptions.cooldown, path.c_str());
        gOptions.startHidden = GetPrivateProfileIntW(L"Apolo", L"StartMinimized",
            gOptions.startHidden ? 1 : 0, path.c_str()) != 0;
    };
    read(gConfig);
    if (gLocked) read(gPolicyFile);
    gOptions.threshold = closest(gOptions.threshold, kLimits);
    gOptions.cooldown = closest(gOptions.cooldown, kIntervals);
}
bool writeConfig() {
    if (gLocked || gConfig.empty()) return false;
    const auto write = [](const wchar_t* key, int value) {
        std::wstring text = std::to_wstring(value);
        return WritePrivateProfileStringW(L"Apolo", key, text.c_str(),
                                           gConfig.c_str()) != FALSE;
    };
    return write(L"AutoCleanEnabled", gOptions.automatic ? 1 : 0) &&
           write(L"AutoCleanThreshold", gOptions.threshold) &&
           write(L"AutoCleanCooldownMinutes", gOptions.cooldown) &&
           write(L"StartMinimized", gOptions.startHidden ? 1 : 0);
}
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
    gLastSnapshot = readMemory();
    const MemorySnapshot& memory = gLastSnapshot;

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


// O painel de desempenho so consulta dados; nenhuma rotina de limpeza e acionada aqui.
void refreshPerformanceDisplay(bool updateProcessList) {
    const auto performance = apolo::samplePerformance(updateProcessList);
    wchar_t buffer[512] = {};

    if (performance.cpuValid) {
        std::swprintf(buffer, std::size(buffer),
                      L"CPU (sistema): %.1f%% em uso", performance.cpuPercent);
    } else {
        std::swprintf(buffer, std::size(buffer),
                      L"CPU (sistema): aguardando amostra...");
    }
    SetWindowTextW(gCpuLabel, buffer);

    if (performance.diskValid) {
        constexpr double toMiB = 1024.0 * 1024.0;
        std::swprintf(buffer, std::size(buffer),
                      L"Disco (E/S total): leitura %.2f MiB/s  |  gravação %.2f MiB/s",
                      performance.diskReadBytesPerSecond / toMiB,
                      performance.diskWriteBytesPerSecond / toMiB);
    } else {
        std::swprintf(buffer, std::size(buffer),
                      L"Disco (E/S total): contadores indisponíveis ou coletando amostras");
    }
    SetWindowTextW(gDiskLabel, buffer);

    if (!performance.processesValid) {
        SetWindowTextW(gProcessLabels[0], L"Consultando processos da sua conta...");
        SetWindowTextW(gProcessLabels[1], L"");
        SetWindowTextW(gProcessLabels[2], L"");
    } else if (performance.processCount == 0) {
        SetWindowTextW(gProcessLabels[0], L"Nenhum processo acessível para consulta.");
        SetWindowTextW(gProcessLabels[1], L"");
        SetWindowTextW(gProcessLabels[2], L"");
    } else {
        for (std::size_t i = 0; i < gProcessLabels.size(); ++i) {
            if (i >= performance.processCount) {
                SetWindowTextW(gProcessLabels[i], L"");
                continue;
            }
            const auto& proc = performance.topProcesses[i];
            std::wstring name = proc.name.substr(0, 28);
            std::swprintf(buffer, std::size(buffer),
                          L"%zu. %ls (PID %lu)  -  %.1f MiB",
                          i + 1, name.c_str(), static_cast<unsigned long>(proc.pid),
                          static_cast<double>(proc.workingSetBytes) / (1024.0 * 1024.0));
            SetWindowTextW(gProcessLabels[i], buffer);
        }
    }

    // Sinais isolados não equivalem a diagnóstico de gargalo.
    if (gLastSnapshot.valid && gLastSnapshot.total > 0 &&
        gLastSnapshot.available * 100ULL < gLastSnapshot.total * 10ULL) {
        SetWindowTextW(gDiagnosisLabel,
                       L"Sinal de atenção: menos de 10% da RAM física disponível.");
    } else if (performance.cpuValid && performance.cpuPercent >= 90.0) {
        SetWindowTextW(gDiagnosisLabel,
                       L"Sinal de atenção: CPU acima de 90% nesta amostra.");
    } else {
        SetWindowTextW(gDiagnosisLabel,
                       L"Leituras instantâneas: ainda não representam diagnóstico de desempenho.");
    }
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


bool history(bool autoMode, const CleanupResult& r) {
    if (gLog.empty() || !r.before.valid || !r.after.valid) return false;
    HANDLE h = CreateFileW(gLog.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length = {};
    bool ok = GetFileSizeEx(h, &length) != FALSE;
    if (ok && length.QuadPart == 0) {
        constexpr char header[] = "data_hora;origem;tratados;elegiveis;ignorados;disponivel_antes_bytes;disponivel_depois_bytes;variacao_bytes\r\n";
        DWORD written = 0;
        ok = WriteFile(h, header, DWORD(sizeof(header)-1), &written, nullptr) != FALSE &&
             written == sizeof(header)-1;
    }
    SYSTEMTIME t = {};
    GetLocalTime(&t);
    char line[512] = {};
    LONGLONG delta = LONGLONG(r.after.available)-LONGLONG(r.before.available);
    int n = std::snprintf(line,sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u;%s;%u;%u;%u;%llu;%llu;%lld\r\n",
        unsigned(t.wYear),unsigned(t.wMonth),unsigned(t.wDay),
        unsigned(t.wHour),unsigned(t.wMinute),unsigned(t.wSecond),
        autoMode ? "automatica" : "manual",r.cleaned,r.eligible,r.skipped,
        static_cast<unsigned long long>(r.before.available),
        static_cast<unsigned long long>(r.after.available),
        static_cast<long long>(delta));
    if (ok && n>0 && size_t(n)<sizeof(line)) {
        DWORD written = 0;
        ok = WriteFile(h,line,DWORD(n),&written,nullptr)!=FALSE &&
             written == DWORD(n);
    } else ok=false;
    CloseHandle(h);
    return ok;
}
void viewHistory(HWND window) {
    if (!exists(gLog)) {
        MessageBoxW(window,L"Nenhuma limpeza registrada.",L"Apolo",MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH] = {};
    if (!GetSystemDirectoryW(path,MAX_PATH)) return;
    std::wstring notepad = std::wstring(path) + L"\\notepad.exe";
    std::wstring args = L"\"" + gLog + L"\"";
    ShellExecuteW(window,L"open",notepad.c_str(),args.c_str(),nullptr,SW_SHOWNORMAL);
}
void cleanNow(HWND window,bool autoMode) {
    if (!autoMode && MessageBoxW(window,
        L"Reduzir o working set de aplicativos pode causar lentidão temporária "
        L"e não garante aumento de desempenho.\n\nDeseja continuar?",
        L"Apolo - Confirmação",MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)!=IDYES) return;
    gLastAttempt = GetTickCount64();
    SetWindowTextW(gStatusLabel,autoMode ? L"Limpeza automática..." : L"Limpeza manual...");
    CleanupResult r = cleanCurrentUserProcesses();
    refreshDisplay();
    bool saved = history(autoMode,r);
    wchar_t text[420] = {};
    double delta = r.before.valid && r.after.valid ?
        toGiB(r.after.available)-toGiB(r.before.available) : 0.0;
    std::swprintf(text,std::size(text),
        L"%s: %u/%u processos (%u ignorados). Variação observada: %+.2f GiB.",
        autoMode ? L"Automática" : L"Manual",r.cleaned,r.eligible,r.skipped,delta);
    std::wstring message(text);
    if(!saved) message += L" Histórico não salvo.";
    SetWindowTextW(gStatusLabel,message.c_str());
}
void openWindow(HWND h) { ShowWindow(h,SW_SHOWNORMAL); SetForegroundWindow(h); }
bool iconAdd(HWND h) {
    NOTIFYICONDATAW n = {};
    n.cbSize=sizeof(n);n.hWnd=h;n.uID=kTrayId;
    n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=kTrayMessage;
    n.hIcon=LoadIconW(nullptr,IDI_INFORMATION);
    wcscpy_s(n.szTip,L"Apolo - Monitor de memória");
    gTray = Shell_NotifyIconW(NIM_ADD,&n)!=FALSE;
    if(gTray) {n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);}
    return gTray;
}
void iconRemove(HWND h) {
    if(!gTray)return;
    NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=h;n.uID=kTrayId;
    Shell_NotifyIconW(NIM_DELETE,&n);gTray=false;
}
void trayMenu(HWND h) {
    HMENU menu=CreatePopupMenu();if(!menu)return;
    AppendMenuW(menu,MF_STRING,kOpenMenu,L"Abrir");
    AppendMenuW(menu,MF_STRING,kCleanMenu,L"Limpar agora...");
    AppendMenuW(menu,MF_STRING,kHistoryMenu,L"Ver histórico");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,kExitMenu,L"Sair do Apolo");
    POINT p={};GetCursorPos(&p);SetForegroundWindow(h);
    TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_BOTTOMALIGN|TPM_LEFTALIGN,
                   p.x,p.y,0,h,nullptr);
    PostMessageW(h,WM_NULL,0,0);DestroyMenu(menu);
}
void combo(HWND h,const int* items,size_t n,int selected,const wchar_t* suffix) {
    for(size_t i=0;i<n;++i) {
        std::wstring t=std::to_wstring(items[i])+suffix;
        SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));
        if(items[i]==selected)SendMessageW(h,CB_SETCURSEL,i,0);
    }
}
void saveUi(HWND window) {
    if(gLocked)return;
    bool enabled=SendMessageW(gAutomatic,BM_GETCHECK,0,0)==BST_CHECKED;
    if(enabled && !gOptions.automatic &&
       MessageBoxW(window,L"Limpeza automática pode causar lentidão. "
       L"Ative somente após teste em máquina piloto.\n\nContinuar?",
       L"Apolo - Automação",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) {
        SendMessageW(gAutomatic,BM_SETCHECK,BST_UNCHECKED,0);return;
    }
    LRESULT a=SendMessageW(gThreshold,CB_GETCURSEL,0,0);
    LRESULT b=SendMessageW(gInterval,CB_GETCURSEL,0,0);
    if(a<0||b<0||size_t(a)>=kLimits.size()||size_t(b)>=kIntervals.size())return;
    Options previous=gOptions;
    gOptions.automatic=enabled;gOptions.threshold=kLimits[size_t(a)];
    gOptions.cooldown=kIntervals[size_t(b)];
    gOptions.startHidden=SendMessageW(gStartHidden,BM_GETCHECK,0,0)==BST_CHECKED;
    if(!writeConfig()) {
        gOptions=previous;
        MessageBoxW(window,L"Falha ao salvar configurações.",L"Apolo",MB_ICONERROR);
        return;
    }
    gLastAttempt=GetTickCount64();
    SetWindowTextW(gStatusLabel,L"Configurações salvas. Intervalo de segurança reiniciado.");
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
    if(gTaskbarCreated && message==gTaskbarCreated) {iconAdd(window);return 0;}
    switch (message) {
    case WM_CREATE: {
        gFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gTitleFont = CreateFontW(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        HWND title = addControl(window, L"STATIC", L"APOLO | Desempenho",
                                SS_LEFT, 25, 16, 735, 48);
        if (gTitleFont) SendMessageW(title, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(gTitleFont), TRUE);
        addControl(window, L"STATIC", L"Monitor de recursos | v0.3.0 (piloto)",
                   SS_LEFT, 27, 68, 735, 28);

        gRamLabel = addControl(window, L"STATIC", L"Consultando memória...",
                               SS_LEFT, 28, 119, 748, 30);
        gCommitLabel = addControl(window, L"STATIC", L"", SS_LEFT, 28, 150, 748, 30);
        gCpuLabel = addControl(window, L"STATIC", L"CPU: aguardando amostra...",
                               SS_LEFT, 28, 188, 748, 30);
        gDiskLabel = addControl(window, L"STATIC", L"Disco: aguardando amostra...",
                                SS_LEFT, 28, 222, 748, 30);
        gDiagnosisLabel = addControl(window, L"STATIC", L"",
                                     SS_LEFT, 28, 260, 748, 31);

        addControl(window, L"BUTTON", L"Atualizar", BS_PUSHBUTTON,
                   28, 306, 145, 39, kRefreshButton);
        addControl(window, L"BUTTON", L"Limpar memória (manual)", BS_PUSHBUTTON,
                   188, 306, 260, 39, kCleanButton);
        addControl(window, L"BUTTON", L"Ver histórico", BS_PUSHBUTTON,
                   462, 306, 176, 39, kHistoryButton);

        addControl(window, L"STATIC", L"PROCESSOS COM MAIOR WORKING SET (SUA CONTA)",
                   SS_LEFT, 28, 373, 747, 27);
        for (std::size_t i = 0; i < gProcessLabels.size(); ++i) {
            gProcessLabels[i] = addControl(
                window, L"STATIC", i == 0 ? L"Consultando processos..." : L"",
                SS_LEFT, 28, 404 + static_cast<int>(i) * 29, 746, 28);
        }

        addControl(window, L"STATIC", L"CONFIGURAÇÕES",
                   SS_LEFT, 28, 505, 430, 29);
        gAutomatic = addControl(window, L"BUTTON", L"Ativar limpeza automática",
                                BS_AUTOCHECKBOX | WS_TABSTOP,
                                28, 536, 350, 30, kAuto);
        gStartHidden = addControl(window, L"BUTTON", L"Iniciar na bandeja",
                                  BS_AUTOCHECKBOX | WS_TABSTOP,
                                  427, 536, 285, 30, kStartHidden);
        addControl(window, L"STATIC", L"Limiar de RAM", SS_LEFT, 28, 590, 143, 28);
        gThreshold = addControl(window, L"COMBOBOX", L"",
                                CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                175, 577, 103, 192, kThreshold);
        addControl(window, L"STATIC", L"Intervalo mínimo", SS_LEFT,
                   297, 590, 172, 28);
        gInterval = addControl(window, L"COMBOBOX", L"",
                               CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                               474, 577, 103, 192, kInterval);
        addControl(window, L"STATIC", L"min", SS_LEFT, 584, 590, 39, 28);
        gSaveButton = addControl(window, L"BUTTON", L"Salvar", BS_PUSHBUTTON,
                                 646, 576, 100, 40, kSave);
        addControl(window, L"STATIC",
                   gLocked ? L"Política corporativa ativa: opções bloqueadas pela TI."
                           : L"Automação desativada por padrão para proteger o desempenho.",
                   SS_LEFT, 28, 628, 735, 29);
        gStatusLabel = addControl(window, L"STATIC", L"Pronto.",
                                  SS_LEFT, 28, 675, 738, 64);

        combo(gThreshold, kLimits.data(), kLimits.size(), gOptions.threshold, L"%");
        combo(gInterval, kIntervals.data(), kIntervals.size(), gOptions.cooldown, L"");
        SendMessageW(gAutomatic, BM_SETCHECK,
                     gOptions.automatic ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(gStartHidden, BM_SETCHECK,
                     gOptions.startHidden ? BST_CHECKED : BST_UNCHECKED, 0);

        if (gLocked) {
            EnableWindow(gAutomatic, FALSE);
            EnableWindow(gStartHidden, FALSE);
            EnableWindow(gThreshold, FALSE);
            EnableWindow(gInterval, FALSE);
            EnableWindow(gSaveButton, FALSE);
        }

        apolo::initializePerformance();
        iconAdd(window);
        gLastAttempt = GetTickCount64();
        SetTimer(window, kRefreshTimer, kRefreshIntervalMs, nullptr);
        refreshDisplay();
        return 0;
    }

    case WM_TIMER:
        if(wParam==kRefreshTimer) {
            refreshDisplay();
            refreshPerformanceDisplay((++gPerformanceTicks % 2) == 1);
            ULONGLONG elapsed=GetTickCount64()-gLastAttempt;
            ULONGLONG interval=ULONGLONG(gOptions.cooldown)*60'000ULL;
            if(gOptions.automatic && gLastSnapshot.valid &&
               gLastSnapshot.loadPercent>=DWORD(gOptions.threshold) &&
               elapsed>=interval) cleanNow(window,true);
        }
        return 0;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case kRefreshButton:
            refreshDisplay();
            refreshPerformanceDisplay(true);
            break;
        case kCleanButton:
        case kCleanMenu:cleanNow(window,false);break;
        case kHistoryButton:
        case kHistoryMenu:viewHistory(window);break;
        case kSave:saveUi(window);break;
        case kOpenMenu:openWindow(window);break;
        case kExitMenu:gExiting=true;DestroyWindow(window);break;
        }
        return 0;
    case kTrayMessage: {
        UINT e=LOWORD(lParam);
        if(e==WM_LBUTTONDBLCLK||e==NIN_SELECT||e==NIN_KEYSELECT)openWindow(window);
        else if(e==WM_RBUTTONUP||e==WM_CONTEXTMENU)trayMenu(window);
        return 0;
    }
    case WM_CLOSE:
        if(!gExiting && gTray) {ShowWindow(window,SW_HIDE);return 0;}
        DestroyWindow(window);return 0;

    case WM_DESTROY:
        KillTimer(window, kRefreshTimer);
        apolo::shutdownPerformance();
        iconRemove(window);
        if(gTitleFont)DeleteObject(gTitleFont);
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
    loadConfig();
    gTaskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    HWND running=FindWindowW(kWindowClass,nullptr);
    if(running){openWindow(running);return 0;}

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
        CW_USEDEFAULT, CW_USEDEFAULT, 810, 798,
        nullptr, nullptr, instance, nullptr);

    if (!window) {
        return 2;
    }

    if(!gOptions.startHidden || !gTray) {
        ShowWindow(window,showCommand);
        UpdateWindow(window);
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
