#include "modern_ui.h"
#include "analysis.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace apolo {
namespace {
template<class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

ID2D1Factory* gFactory = nullptr;
ID2D1HwndRenderTarget* gTarget = nullptr;
ID2D1SolidColorBrush* gBrush = nullptr;
IDWriteFactory* gWrite = nullptr;
IDWriteTextFormat* gCaption = nullptr;
IDWriteTextFormat* gBody = nullptr;
IDWriteTextFormat* gMedium = nullptr;
IDWriteTextFormat* gTitle = nullptr;
IDWriteTextFormat* gBig = nullptr;
IDWriteTextFormat* gWrap = nullptr;
HWND gHost = nullptr;

constexpr UINT32 kBackground = 0x0E131A;
constexpr UINT32 kSidebar = 0x111B26;
constexpr UINT32 kPanel = 0x192330;
constexpr UINT32 kPanelHover = 0x233346;
constexpr UINT32 kBorder = 0x2B3949;
constexpr UINT32 kText = 0xEBF2FA;
constexpr UINT32 kMuted = 0x96A7B9;
constexpr UINT32 kBlue = 0x3AA0FF;
constexpr UINT32 kTeal = 0x4ED7C5;
constexpr UINT32 kAmber = 0xE6B450;

struct Hit {
    D2D1_RECT_F area{};
    int action = 0;
};
enum HitAction {
    kRefresh = 1, kClean, kOpenCsv, kSave,
    kToggleAuto, kToggleHidden, kThreshold, kCooldown,
    kNavBase = 100, kRangeBase = 200, kSortBase = 300
};
std::array<Hit, 48> gHits{};
std::size_t gHitCount = 0;

struct LogItem {
    std::wstring date;
    std::wstring origin;
    unsigned treated = 0;
    double deltaMiB = 0;
};
std::array<LogItem, 40> gLogs{};
std::size_t gLogCount = 0;
int gPage = 0, gRange = 1, gSort = 0, gScroll = 0, gHistoryScroll = 0;
UiSnapshot gData;
UiSettings gSettings;
std::wstring gStatus = L"Pronto para monitorar. Sem serviços de rede.";
float gWidth = 1100, gHeight = 780;
float gSide = 220, gX = 248, gContentW = 800;

D2D1_COLOR_F color(UINT32 rgb, float opacity = 1.0f) {
    return D2D1::ColorF(rgb, opacity);
}
D2D1_RECT_F rect(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + std::max(0.0f, w), y + std::max(0.0f, h));
}
void brush(UINT32 rgb, float alpha=1) { gBrush->SetColor(color(rgb, alpha)); }
void roundFill(float x,float y,float w,float h,float radius,UINT32 rgb) {
    brush(rgb);
    gTarget->FillRoundedRectangle(D2D1::RoundedRect(rect(x,y,w,h),radius,radius),gBrush);
}
void roundStroke(float x,float y,float w,float h,float radius,UINT32 rgb) {
    brush(rgb);
    gTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect(x,y,w,h),radius,radius),gBrush,1.0f);
}
void line(float x1,float y1,float x2,float y2,UINT32 rgb,float weight=1) {
    brush(rgb);
    gTarget->DrawLine(D2D1::Point2F(x1,y1),D2D1::Point2F(x2,y2),gBrush,weight);
}
void text(const std::wstring& value, float x,float y,float w,float h,
          IDWriteTextFormat* font,UINT32 rgb=kText) {
    if(value.empty() || !font)return;
    brush(rgb);
    gTarget->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()),font,
                       rect(x,y,w,h),gBrush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void track(float x,float y,float w,float percentage,UINT32 rgb) {
    roundFill(x,y,w,8,4,kBorder);
    const float length=w*std::clamp(percentage,0.0f,100.0f)/100.0f;
    if(length>2)roundFill(x,y,length,8,4,rgb);
}
void hit(float x,float y,float w,float h,int action) {
    if(gHitCount<gHits.size())gHits[gHitCount++]={rect(x,y,w,h),action};
}
void pill(float x,float y,float w,float h,const std::wstring& label,
          bool selected,int action) {
    roundFill(x,y,w,h,9,selected?kPanelHover:kPanel);
    roundStroke(x,y,w,h,9,selected?kBlue:kBorder);
    text(label,x+13,y+8,w-22,h-11,gCaption,selected?kBlue:kMuted);
    if(action)hit(x,y,w,h,action);
}
void button(float x,float y,float w,float h,const std::wstring& label,
            int action,bool primary=false) {
    roundFill(x,y,w,h,10,primary?kBlue:kPanelHover);
    if(!primary)roundStroke(x,y,w,h,10,kBorder);
    text(label,x+15,y+(h-19)*0.5f,w-24,23,gBody,primary?kBackground:kText);
    if(action)hit(x,y,w,h,action);
}
std::wstring format(const wchar_t* pattern,double n) {
    wchar_t buffer[120]={};
    std::swprintf(buffer,std::size(buffer),pattern,n);
    return buffer;
}
bool resources() {
    if(!gFactory||!gHost)return false;
    if(gTarget && gBrush)return true;
    RECT client={};
    GetClientRect(gHost,&client);
    const auto dims=D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(1,client.right)),
        static_cast<UINT32>(std::max<LONG>(1,client.bottom)));
    HRESULT hr=gFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(gHost,dims),&gTarget);
    if(FAILED(hr))return false;
    // Coordenadas em pixels para manter mouse e desenho alinhados.
    gTarget->SetDpi(96.0f,96.0f);
    hr=gTarget->CreateSolidColorBrush(color(kText),&gBrush);
    if(FAILED(hr)) {
        release(gTarget);
        return false;
    }
    return true;
}
void discardTarget() { release(gBrush);release(gTarget); }
void updateGeometry() {
    gSide=gWidth<1030?190.0f:222.0f;
    gX=gSide+27;
    gContentW=std::max(380.0f,gWidth-gX-26.0f);
}
void paintFrame() {
    gHitCount=0;
    updateGeometry();
    gTarget->Clear(color(kBackground));
    brush(kSidebar);
    gTarget->FillRectangle(rect(0,0,gSide,gHeight),gBrush);
    line(gSide,0,gSide,gHeight,kBorder);

    text(L"◉",29,30,36,42,gTitle,kTeal);
    text(L"APOLO",64,30,gSide-70,48,gTitle,kText);
    text(L"PERFORMANCE ENGINE",31,80,gSide-35,25,gCaption,kMuted);
    line(22,125,gSide-22,125,kBorder);

    const std::array<const wchar_t*,5> menu={
        L"Visão geral",L"Desempenho",L"Processos",L"Histórico",L"Configurações"
    };
    for(int i=0;i<5;++i) {
        const float y=157.0f+i*58.0f;
        if(gPage==i) {
            roundFill(17,y,gSide-33,45,9,kPanelHover);
            roundFill(17,y+7,3,31,1,kBlue);
        }
        text(menu[i],35,y+11,gSide-55,28,gBody,gPage==i?kText:kMuted);
        hit(17,y,gSide-33,46,kNavBase+i);
    }
    text(L"NATIVO  /  OFFLINE",26,gHeight-96,gSide-40,24,gCaption,kTeal);
    text(L"Versão 0.6  •  Piloto",26,gHeight-69,gSide-39,25,gCaption,kMuted);

    const wchar_t* titles[]={L"Visão geral",L"Desempenho",L"Processos",
                             L"Histórico",L"Configurações"};
    const wchar_t* sub[]={
        L"Saúde do sistema e principais indicadores",
        L"Histórico de recursos em tempo real",
        L"Memória residente dos processos da sua sessão",
        L"Operações de limpeza registradas neste computador",
        L"Preferências locais de monitoramento e limpeza"
    };
    text(titles[gPage],gX,29,gContentW-210,48,gTitle);
    text(sub[gPage],gX,78,gContentW-10,30,gBody,kMuted);
    roundFill(gX+gContentW-170,37,170,31,14,kPanel);
    text(L"● MONITOR LOCAL",gX+gContentW-158,44,151,22,gCaption,kTeal);
    line(gX,128,gX+gContentW,128,kBorder);
}

void metricCard(float x,float y,float w,const wchar_t* heading,
                const std::wstring& value,const std::wstring& description,
                float percent,UINT32 accent,bool showTrack=true) {
    roundFill(x,y,w,155,14,kPanel);
    roundStroke(x,y,w,155,14,kBorder);
    text(heading,x+19,y+16,w-40,24,gCaption,kMuted);
    text(value,x+19,y+43,w-40,57,gBig,kText);
    if(showTrack)track(x+19,y+108,w-38,percent,accent);
    text(description,x+19,y+123,w-38,25,gCaption,kMuted);
}

void drawGraph(float x,float y,float w,float h,int mode) {
    const UINT32 accent=mode==0?kBlue:mode==1?kTeal:kAmber;
    const wchar_t* title=mode==0?L"CPU":mode==1?L"Memória RAM":
                           mode==2?L"Disco  •  MiB/s":L"Tendência de uso  •  CPU / RAM";
    roundFill(x,y,w,h,13,kPanel);
    roundStroke(x,y,w,h,13,kBorder);
    text(title,x+18,y+12,w-30,24,gMedium,kText);
    const float x1=x+22,x2=x+w-20,y1=y+44,y2=y+h-25;
    for(int j=0;j<=3;++j) {
        const float yp=y1+(y2-y1)*static_cast<float>(j)/3.0f;
        line(x1,yp,x2,yp,kBorder);
    }
    std::array<HistoryPoint,360> points{};
    const auto count=copyHistoryPoints(points.data(),points.size());
    const ULONGLONG now=GetTickCount64();
    const ULONGLONG span=gRange==0?60000ULL:gRange==1?300000ULL:900000ULL;
    double maxDisk=4.0;
    if(mode==2) for(std::size_t i=0;i<count;++i)
        if(points[i].diskValid && points[i].timestamp<=now &&
           now-points[i].timestamp<=span)
            maxDisk=std::max(maxDisk,points[i].diskMiBPerSecond);
    auto drawSeries=[&](int series,UINT32 rgb) {
        bool prior=false,any=false;
        float px=0,py=0;
        ULONGLONG previous=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& p=points[i];
            if(p.timestamp>now||now-p.timestamp>span)continue;
            bool valid=series==0?p.cpuValid:series==1?p.ramValid:p.diskValid;
            if(!valid){prior=false;continue;}
            double v=series==0?p.cpuPercent:series==1?p.ramPercent:p.diskMiBPerSecond;
            double max=series==2?maxDisk:100.0;
            const float xx=x2-static_cast<float>(now-p.timestamp) /
                                  static_cast<float>(span)*(x2-x1);
            const float yy=y2-static_cast<float>(std::clamp(v/max,0.0,1.0))*(y2-y1);
            if(prior && p.timestamp>=previous && p.timestamp-previous<=8000)
                line(px,py,xx,yy,rgb,2.0f);
            px=xx;py=yy;previous=p.timestamp;prior=true;any=true;
        }
        return any;
    };
    bool any=false;
    if(mode==3) {
        any=drawSeries(0,kBlue);
        any=drawSeries(1,kTeal)||any;
        text(L"CPU",x+w-140,y+12,50,25,gCaption,kBlue);
        text(L"RAM",x+w-75,y+12,55,25,gCaption,kTeal);
    } else {
        any=drawSeries(mode,accent);
    }
    if(!any)text(L"Aguardando leituras...",x+30,y+65,w-60,30,gBody,kMuted);
}

std::vector<ProcessSample> processList() {
    std::array<ProcessSample,24> buffer{};
    const std::size_t n=copyRecentProcesses(buffer.data(),buffer.size());
    std::vector<ProcessSample> result(buffer.begin(),buffer.begin()+n);
    if(gSort==1)
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
            int n=_wcsicmp(a.name.c_str(),b.name.c_str());
            return n<0||(n==0&&a.pid<b.pid);
        });
    else if(gSort==2)
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
            return a.pid<b.pid;
        });
    return result;
}
void processRows(float y,int maximum,bool compact) {
    auto rows=processList();
    if(rows.empty()) {
        text(L"Consultando processos acessíveis...",gX+17,y+18,
             gContentW-34,28,gBody,kMuted);
        return;
    }
    const int start=compact?0:std::min(gScroll,static_cast<int>(rows.size())-1);
    const int end=std::min(static_cast<int>(rows.size()),start+maximum);
    for(int i=start;i<end;++i) {
        const auto& p=rows[static_cast<std::size_t>(i)];
        const float yy=y+(i-start)*34.0f;
        if((i-start)%2==0)roundFill(gX+7,yy,gContentW-14,32,6,kPanelHover);
        text(p.name.substr(0,30),gX+20,yy+6,gContentW*0.54f,26,gBody,kText);
        text(std::to_wstring(p.pid),gX+gContentW*0.60f,yy+6,95,24,gCaption,kMuted);
        text(format(L"%.1f MiB",static_cast<double>(p.workingSetBytes)/1048576.0),
             gX+gContentW-145,yy+6,135,27,gBody,kTeal);
    }
}
void overview() {
    const float gap=14,cardW=(gContentW-2*gap)/3.0f;
    metricCard(gX,155,cardW,L"MEMÓRIA RAM",
        gData.ramValid?format(L"%.0f%%",gData.ramPercent):L"—",
        gData.ramValid?format(L"%.1f GiB disponíveis",gData.ramAvailableGiB):
            L"Coletando...",static_cast<float>(gData.ramPercent),kTeal);
    metricCard(gX+cardW+gap,155,cardW,L"PROCESSADOR",
        gData.cpuValid?format(L"%.0f%%",gData.cpuPercent):L"—",
        gData.cpuValid?L"Utilização total":L"Aguardando amostra",
        static_cast<float>(gData.cpuPercent),kBlue);
    metricCard(gX+2*(cardW+gap),155,cardW,L"ATIVIDADE DO DISCO",
        gData.diskValid?format(L"%.1f MiB/s",gData.diskReadMiB+gData.diskWriteMiB):
            L"—",gData.diskValid?L"Leitura + gravação":L"Contadores indisponíveis",
        0,kAmber,false);

    const float graphY=328,graphH=185;
    drawGraph(gX,graphY,gContentW*0.59f-8,graphH,3);
    const float dx=gX+gContentW*0.59f+7;
    roundFill(dx,graphY,gContentW*0.41f-7,graphH,13,kPanel);
    roundStroke(dx,graphY,gContentW*0.41f-7,graphH,13,kBorder);
    text(L"DIAGNÓSTICO",dx+17,graphY+15,gContentW*0.41f-35,28,gCaption,kTeal);
    text(gData.diagnosis,dx+17,graphY+48,gContentW*0.41f-35,106,gWrap,kText);

    const float rowY=535;
    text(L"PRINCIPAIS PROCESSOS",gX+8,rowY,gContentW*0.5f,29,gMedium,kText);
    button(gX+gContentW-304,rowY-5,108,34,L"Atualizar",kRefresh);
    button(gX+gContentW-184,rowY-5,184,34,L"Limpar memória",kClean,true);
    const float top=rowY+43;
    processRows(top,3,true);
}

void performancePage() {
    text(L"Último período",gX+3,149,140,29,gCaption,kMuted);
    const float bx=gX+gContentW-323;
    pill(bx,141,97,33,L"1 min",gRange==0,kRangeBase);
    pill(bx+105,141,97,33,L"5 min",gRange==1,kRangeBase+1);
    pill(bx+210,141,113,33,L"15 min",gRange==2,kRangeBase+2);
    const float available=std::max(405.0f,gHeight-216.0f);
    const float chartH=std::max(105.0f,(available-26.0f)/3.0f);
    for(int i=0;i<3;++i)
        drawGraph(gX,185+i*(chartH+13),gContentW,chartH,i);
}

void processesPage() {
    roundFill(gX,153,gContentW,gHeight-223,13,kPanel);
    roundStroke(gX,153,gContentW,gHeight-223,13,kBorder);
    text(L"PROCESSOS ACESSÍVEIS  •  ATÉ 24",gX+17,170,gContentW-350,30,
         gMedium,kText);
    const float bx=gX+gContentW-350;
    pill(bx,165,110,35,L"RAM",gSort==0,kSortBase);
    pill(bx+119,165,110,35,L"Nome",gSort==1,kSortBase+1);
    pill(bx+238,165,110,35,L"PID",gSort==2,kSortBase+2);
    line(gX+10,215,gX+gContentW-10,215,kBorder);
    text(L"PROCESSO",gX+20,225,260,27,gCaption,kMuted);
    text(L"PID",gX+gContentW*0.60f,225,95,27,gCaption,kMuted);
    text(L"WORKING SET",gX+gContentW-145,225,140,27,gCaption,kMuted);
    const int rows=std::max(1,static_cast<int>((gHeight-335)/34));
    processRows(258,rows,false);
    text(L"Roda da mouse para rolar. Apenas a sua conta e sessão; sem encerrar processos.",
         gX+16,gHeight-95,gContentW-30,24,gCaption,kMuted);
}

void historyPage() {
    text(L"Últimas operações de limpeza",gX,153,gContentW*0.65f,33,gMedium);
    button(gX+gContentW-168,151,168,39,L"Abrir CSV",kOpenCsv);
    roundFill(gX,214,gContentW,gHeight-280,13,kPanel);
    roundStroke(gX,214,gContentW,gHeight-280,13,kBorder);
    text(L"DATA E HORA",gX+20,233,225,23,gCaption,kMuted);
    text(L"ORIGEM",gX+gContentW*0.43f,233,125,23,gCaption,kMuted);
    text(L"PROCESSOS",gX+gContentW*0.64f,233,130,23,gCaption,kMuted);
    text(L"VARIAÇÃO RAM",gX+gContentW-160,233,150,23,gCaption,kMuted);
    line(gX+10,267,gX+gContentW-10,267,kBorder);
    if(!gLogCount)text(L"Sem operações registradas. A limpeza manual gera um registro.",
                       gX+20,287,gContentW-35,36,gBody,kMuted);
    const int rows=std::max(1,static_cast<int>((gHeight-350)/35));
    const int start=std::min(gHistoryScroll,static_cast<int>(gLogCount));
    const int end=std::min(static_cast<int>(gLogCount),start+rows);
    for(int i=start;i<end;++i) {
        const auto& item=gLogs[static_cast<std::size_t>(gLogCount-1-i)];
        float yy=276.0f+(i-start)*35.0f;
        if((i-start)%2==0)roundFill(gX+8,yy,gContentW-16,33,7,kPanelHover);
        text(item.date,gX+20,yy+7,215,25,gCaption,kText);
        text(item.origin,gX+gContentW*0.43f,yy+7,130,25,gCaption,kMuted);
        text(std::to_wstring(item.treated),gX+gContentW*0.64f,yy+7,110,25,gCaption,kText);
        text(format(L"%+.1f MiB",item.deltaMiB),
             gX+gContentW-160,yy+7,150,26,gCaption,kTeal);
    }
    text(L"Histórico local • a variação de RAM não é ganho de velocidade.",
         gX+12,gHeight-97,gContentW-20,25,gCaption,kMuted);
}

void settingRow(float x,float y,const std::wstring& heading,
                const std::wstring& description,bool checked,int action) {
    const float w=gContentW-34;
    roundFill(x,y,w,84,12,kPanelHover);
    text(heading,x+20,y+12,w-100,27,gMedium);
    text(description,x+20,y+42,w-100,26,gCaption,kMuted);
    roundFill(x+w-78,y+24,54,30,15,checked?kBlue:kBorder);
    roundFill(x+w-74+(checked?23.0f:0.0f),y+28,22,22,11,kText);
    if(!gSettings.locked)hit(x,y,w,84,action);
}
void settingsPage() {
    roundFill(gX,154,gContentW,gHeight-222,14,kPanel);
    roundStroke(gX,154,gContentW,gHeight-222,14,kBorder);
    const float x=gX+17,w=gContentW-34;
    text(L"PREFERÊNCIAS",x+4,174,w-20,35,gMedium);
    text(gSettings.locked?L"Política corporativa ativa  •  controles bloqueados"
                         :L"As alterações só são aplicadas ao clicar em Salvar.",
         x+4,205,w-20,26,gCaption,gSettings.locked?kAmber:kMuted);
    settingRow(x,244,L"Limpeza automática",
        L"Desativada por padrão. Pode afetar a resposta dos aplicativos.",
        gSettings.automatic,kToggleAuto);
    settingRow(x,342,L"Abrir minimizado na bandeja",
        L"Não configura inicialização automática com o Windows.",
        gSettings.startHidden,kToggleHidden);
    text(L"Limite de uso da RAM",x+6,450,w*0.48f,28,gCaption,kMuted);
    text(L"Intervalo mínimo",x+w*0.52f,450,w*0.45f,28,gCaption,kMuted);
    pill(x,481,w*0.45f,48,std::to_wstring(gSettings.threshold)+L"%  •  alterar",
         false,gSettings.locked?0:kThreshold);
    pill(x+w*0.52f,481,w*0.46f,48,
         std::to_wstring(gSettings.cooldown)+L" min  •  alterar",
         false,gSettings.locked?0:kCooldown);
    button(x,552,std::min(290.0f,w),45,
           gSettings.locked?L"Gerenciado pela TI":L"Salvar configurações",
           gSettings.locked?0:kSave,true);
    text(L"A limpeza automática permanece independente dos avisos de diagnóstico.",
         x+5,616,w-10,45,gCaption,kMuted);
}

void paintStatus() {
    const float y=gHeight-54;
    line(gX,y-9,gX+gContentW,y-9,kBorder);
    text(gStatus,gX+4,y,gContentW-13,42,gCaption,kMuted);
}

bool makeFonts() {
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&gWrite))))
        return false;
    auto mk=[](float size,DWRITE_FONT_WEIGHT weight,IDWriteTextFormat** out) {
        HRESULT hr=gWrite->CreateTextFormat(L"Segoe UI",nullptr,weight,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
            size,L"pt-BR",out);
        if(SUCCEEDED(hr))(*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return SUCCEEDED(hr);
    };
    if(!mk(12,DWRITE_FONT_WEIGHT_NORMAL,&gCaption)||
       !mk(15,DWRITE_FONT_WEIGHT_NORMAL,&gBody)||
       !mk(17,DWRITE_FONT_WEIGHT_SEMI_BOLD,&gMedium)||
       !mk(28,DWRITE_FONT_WEIGHT_BOLD,&gTitle)||
       !mk(37,DWRITE_FONT_WEIGHT_SEMI_BOLD,&gBig)||
       !mk(14,DWRITE_FONT_WEIGHT_NORMAL,&gWrap))
        return false;
    gWrap->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    return true;
}

std::wstring wide(const std::string& input) {
    if(input.empty())return L"";
    int len=MultiByteToWideChar(CP_UTF8,0,input.data(),
                                static_cast<int>(input.size()),nullptr,0);
    if(len<=0)return L"";
    std::wstring output(static_cast<std::size_t>(len),L'\0');
    MultiByteToWideChar(CP_UTF8,0,input.data(),
                        static_cast<int>(input.size()),output.data(),len);
    return output;
}

} // namespace

bool modernCreate(HWND host) {
    gHost=host;
    HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&gFactory);
    if(FAILED(hr)||!makeFonts()||!resources()) {
        modernDestroy();
        return false;
    }
    return true;
}
void modernDestroy() {
    discardTarget();
    release(gCaption);release(gBody);release(gMedium);
    release(gTitle);release(gBig);release(gWrap);
    release(gWrite);release(gFactory);
    gHost=nullptr;
}
void modernResize(unsigned w,unsigned h) {
    gWidth=static_cast<float>(w);
    gHeight=static_cast<float>(h);
    if(gTarget) {
        HRESULT hr=gTarget->Resize(D2D1::SizeU(w,h));
        if(hr==D2DERR_RECREATE_TARGET)discardTarget();
    }
}
void modernSetSnapshot(const UiSnapshot& data) { gData=data; }
void modernSetSettings(const UiSettings& settings) { gSettings=settings; }
UiSettings modernGetSettings() { return gSettings; }
void modernSetStatus(const std::wstring& status) {gStatus=status;}

void modernLoadHistoryCsv(const std::wstring& path) {
    gLogCount=0;
    gHistoryScroll=0;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size={};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<=0){CloseHandle(file);return;}
    constexpr DWORD kMaxRead=65536;
    const DWORD bytes=static_cast<DWORD>(std::min<LONGLONG>(
        size.QuadPart,static_cast<LONGLONG>(kMaxRead)));
    const bool truncated=size.QuadPart>bytes;
    if(truncated) {
        LARGE_INTEGER offset={};
        offset.QuadPart=size.QuadPart-bytes;
        SetFilePointerEx(file,offset,nullptr,FILE_BEGIN);
    }
    std::string data(bytes,'\0');
    DWORD read=0;
    bool ok=ReadFile(file,data.data(),bytes,&read,nullptr)!=FALSE;
    CloseHandle(file);
    if(!ok)return;
    data.resize(read);
    std::size_t pos=truncated?data.find('\n')+1:0;
    if(pos==0||pos>data.size())pos=0;
    while(pos<data.size()) {
        const auto end=data.find('\n',pos);
        if(end==std::string::npos)break;
        std::string row=data.substr(pos,end-pos);
        if(!row.empty()&&row.back()=='\r')row.pop_back();
        pos=end+1;
        if(row.empty()||row.rfind("data_hora",0)==0)continue;
        std::array<std::string,8> fields{};
        std::size_t cursor=0;
        bool valid=true;
        for(int i=0;i<7;++i) {
            auto semi=row.find(';',cursor);
            if(semi==std::string::npos){valid=false;break;}
            fields[static_cast<std::size_t>(i)]=row.substr(cursor,semi-cursor);
            cursor=semi+1;
        }
        if(!valid)continue;
        fields[7]=row.substr(cursor);
        if(fields[0].size()>25||fields[1].size()>24)continue;
        LogItem entry;
        entry.date=wide(fields[0]);
        entry.origin=wide(fields[1]);
        entry.treated=static_cast<unsigned>(std::strtoul(fields[2].c_str(),nullptr,10));
        entry.deltaMiB=std::strtoll(fields[7].c_str(),nullptr,10)/1048576.0;
        if(gLogCount<gLogs.size())gLogs[gLogCount++]=std::move(entry);
        else {
            for(std::size_t i=1;i<gLogs.size();++i)gLogs[i-1]=std::move(gLogs[i]);
            gLogs.back()=std::move(entry);
        }
    }
}
UiAction modernClick(HWND window,int x,int y) {
    for(std::size_t i=gHitCount;i>0;--i) {
        const auto& h=gHits[i-1];
        if(x<h.area.left||x>h.area.right||y<h.area.top||y>h.area.bottom)continue;
        int action=h.action;
        if(action>=kNavBase && action<kNavBase+5) {
            gPage=action-kNavBase;gScroll=gHistoryScroll=0;
            InvalidateRect(window,nullptr,FALSE);
            return gPage==3?UiAction::LoadHistory:UiAction::None;
        }
        if(action>=kRangeBase && action<kRangeBase+3) {
            gRange=action-kRangeBase;InvalidateRect(window,nullptr,FALSE);
            return UiAction::None;
        }
        if(action>=kSortBase && action<kSortBase+3) {
            gSort=action-kSortBase;gScroll=0;
            InvalidateRect(window,nullptr,FALSE);
            return UiAction::None;
        }
        if(action==kToggleAuto && !gSettings.locked)gSettings.automatic=!gSettings.automatic;
        if(action==kToggleHidden && !gSettings.locked)gSettings.startHidden=!gSettings.startHidden;
        if(action==kThreshold && !gSettings.locked) {
            constexpr std::array<int,6> list={70,75,80,85,90,95};
            for(std::size_t j=0;j<list.size();++j)
                if(gSettings.threshold==list[j]) {
                    gSettings.threshold=list[(j+1)%list.size()];break;
                }
        }
        if(action==kCooldown && !gSettings.locked) {
            constexpr std::array<int,5> list={15,30,60,120,240};
            for(std::size_t j=0;j<list.size();++j)
                if(gSettings.cooldown==list[j]) {
                    gSettings.cooldown=list[(j+1)%list.size()];break;
                }
        }
        InvalidateRect(window,nullptr,FALSE);
        switch(action) {
        case kRefresh:return UiAction::Refresh;
        case kClean:return UiAction::Clean;
        case kOpenCsv:return UiAction::OpenCsv;
        case kSave:return gSettings.locked?UiAction::None:UiAction::SaveSettings;
        default:return UiAction::None;
        }
    }
    return UiAction::None;
}
void modernWheel(HWND window,int delta) {
    int step=delta>0?-2:2;
    if(gPage==2)gScroll=std::clamp(gScroll+step,0,23);
    if(gPage==3)gHistoryScroll=std::clamp(gHistoryScroll+step,0,39);
    InvalidateRect(window,nullptr,FALSE);
}
bool modernOverControl(int x,int y) {
    for(std::size_t i=0;i<gHitCount;++i) {
        const auto& h=gHits[i];
        if(x>=h.area.left && x<=h.area.right &&
           y>=h.area.top && y<=h.area.bottom)return true;
    }
    return false;
}
void modernPaint(HWND window) {
    PAINTSTRUCT ps={};
    BeginPaint(window,&ps);
    if(resources()) {
        gTarget->BeginDraw();
        paintFrame();
        switch(gPage) {
        case 0:overview();break;
        case 1:performancePage();break;
        case 2:processesPage();break;
        case 3:historyPage();break;
        case 4:settingsPage();break;
        }
        paintStatus();
        HRESULT hr=gTarget->EndDraw();
        if(hr==D2DERR_RECREATE_TARGET)discardTarget();
    }
    EndPaint(window,&ps);
}
} // namespace apolo
