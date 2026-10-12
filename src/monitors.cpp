#include "monitors.h"

#include <dxgi1_2.h>
#include <algorithm>

#include "log.h"
#include "win_util.h"

std::string MonitorInfo::Title() const {
    std::string t = Fmt("Monitor %d", number);
    if (primary) t += " (principal)";
    return t;
}

static BOOL CALLBACK EnumProc(HMONITOR hmon, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) return TRUE;
    MonitorInfo m;
    m.handle = hmon;
    m.deviceName = mi.szDevice;
    m.rect = mi.rcMonitor;
    m.work = mi.rcWork;
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m.dpi = DpiForMonitor(hmon);
    list->push_back(m);
    return TRUE;
}

// Nome "bonito" do monitor (o que aparece em Configurações > Vídeo)
static void FillFriendlyNames(std::vector<MonitorInfo>& mons) {
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) != ERROR_SUCCESS) return;
    for (UINT32 i = 0; i < nPaths; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt = {};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = paths[i].targetInfo.adapterId;
        tgt.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) != ERROR_SUCCESS) continue;
        for (auto& m : mons) {
            if (_wcsicmp(m.deviceName.c_str(), src.viewGdiDeviceName) == 0 && tgt.monitorFriendlyDeviceName[0])
                m.friendlyName = Narrow(tgt.monitorFriendlyDeviceName);
        }
    }
}

// Descobre em qual placa de vídeo/saída (DXGI) está cada monitor. O ddagrab do
// ffmpeg precisa disso para capturar pela GPU certa — em notebooks com duas
// placas (Intel + NVIDIA) a tela interna costuma estar ligada na Intel.
static void FillDxgi(std::vector<MonitorInfo>& mons) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        Log::Warn("CreateDXGIFactory1 falhou — captura DXGI indisponível.");
        return;
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++) {
        DXGI_ADAPTER_DESC1 ad = {};
        adapter->GetDesc1(&ad);
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; o++) {
            DXGI_OUTPUT_DESC od = {};
            if (SUCCEEDED(output->GetDesc(&od))) {
                for (auto& m : mons) {
                    if (m.dxgiAdapter < 0 && (od.Monitor == m.handle || _wcsicmp(od.DeviceName, m.deviceName.c_str()) == 0)) {
                        m.dxgiAdapter = (int)a;
                        m.dxgiOutput = (int)o;
                        // a Desktop Duplication entrega a imagem na orientação "nativa" do
                        // painel; com o monitor girado (90/180/270) o vídeo sairia torto
                        m.rotated = od.Rotation == DXGI_MODE_ROTATION_ROTATE90 || od.Rotation == DXGI_MODE_ROTATION_ROTATE180 ||
                                    od.Rotation == DXGI_MODE_ROTATION_ROTATE270;
                        m.adapterName = Narrow(ad.Description);
                    }
                }
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
}

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> mons;
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, (LPARAM)&mons);
    // numera da esquerda para a direita (e de cima para baixo)
    std::sort(mons.begin(), mons.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    for (size_t i = 0; i < mons.size(); i++) mons[i].number = (int)i + 1;
    FillFriendlyNames(mons);
    FillDxgi(mons);
    return mons;
}

int MonitorIndexForRect(const std::vector<MonitorInfo>& mons, const RECT& r) {
    int best = -1;
    LONG bestArea = -1;
    for (size_t i = 0; i < mons.size(); i++) {
        RECT x;
        if (IntersectRect(&x, &mons[i].rect, &r)) {
            LONG area = (x.right - x.left) * (x.bottom - x.top);
            if (area > bestArea) { bestArea = area; best = (int)i; }
        }
    }
    return best;
}

int MonitorIndexForPoint(const std::vector<MonitorInfo>& mons, POINT p) {
    for (size_t i = 0; i < mons.size(); i++)
        if (PtInRect(&mons[i].rect, p)) return (int)i;
    return mons.empty() ? -1 : 0;
}

HBITMAP CaptureScreenBitmap(const RECT& src) {
    int w = src.right - src.left, h = src.bottom - src.top;
    if (w <= 0 || h <= 0) return nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, src.left, src.top, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return bmp;
}

bool CaptureScreenScaled(const RECT& src, int maxW, int maxH, std::vector<uint8_t>& bgra, int& outW, int& outH) {
    int w = src.right - src.left, h = src.bottom - src.top;
    if (w <= 0 || h <= 0) return false;
    double s = std::min((double)maxW / w, (double)maxH / h);
    if (s > 1.0) s = 1.0;
    outW = std::max(1, (int)(w * s));
    outH = std::max(1, (int)(h * s));

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = outW;
    bi.bmiHeader.biHeight = -outH;     // negativo = linhas de cima para baixo
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        SetStretchBltMode(mem, HALFTONE);
        SetBrushOrgEx(mem, 0, 0, nullptr);
        ok = StretchBlt(mem, 0, 0, outW, outH, screen, src.left, src.top, w, h, SRCCOPY | CAPTUREBLT) != FALSE;
        GdiFlush();
        bgra.assign((uint8_t*)bits, (uint8_t*)bits + (size_t)outW * outH * 4);
        for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;   // BitBlt deixa o alfa zerado
        SelectObject(mem, old);
        DeleteObject(dib);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}
