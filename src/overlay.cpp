#include "overlay.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>

#include "log.h"

#define WM_APP_FINISH (WM_APP + 77)

static RegionSelector* g_sel = nullptr;
static const wchar_t* kOverlayClass = L"SCP_RegionOverlay";
static const wchar_t* kDefaultHint =
    L"Arraste para desenhar a área   •   Clique numa janela para pegá-la inteira   •   Enter grava   •   Esc cancela";

struct EnumCtx {
    std::vector<RECT>* out;
    const std::vector<HWND>* ignore;
};

static BOOL CALLBACK EnumTopWindows(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    for (HWND ig : *ctx->ignore)
        if (ig == h) return TRUE;
    // janelas "fantasmas" do Windows 10/11 (apps suspensos) ficam visíveis mas ocultas pelo DWM
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    wchar_t cls[64] = {0};
    GetClassNameW(h, cls, 63);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return TRUE;   // área de trabalho
    RECT r;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
    if (r.right - r.left < 40 || r.bottom - r.top < 40) return TRUE;
    ctx->out->push_back(r);
    return TRUE;
}

bool RegionSelector::Begin(const std::vector<MonitorInfo>& monitors, const std::vector<HWND>& ignore, Done done) {
    if (Active() || monitors.empty()) return false;
    mons_ = monitors;
    done_ = std::move(done);
    hasSel_ = false;
    drag_ = Drag::None;
    finishing_ = false;
    hoverValid_ = false;
    hoverButton_ = 0;
    hint_ = kDefaultHint;

    // janelas visíveis, da mais de cima para a de baixo (para o "clique na janela")
    std::vector<RECT> rects;
    EnumCtx ctx{&rects, &ignore};
    EnumWindows(EnumTopWindows, (LPARAM)&ctx);
    candidates_.clear();
    for (auto& r : rects) candidates_.push_back({r});

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kOverlayClass;
        wc.hCursor = nullptr;
        RegisterClassExW(&wc);
        registered = true;
    }
    g_sel = this;
    // 1º captura tudo (antes de cobrir a tela), depois cria as janelas
    for (size_t i = 0; i < mons_.size(); i++) {
        auto w = std::make_unique<Win>();
        w->mon = (int)i;
        const RECT& r = mons_[i].rect;
        if (!CaptureScreenScaled(r, r.right - r.left, r.bottom - r.top, w->pixels, w->pw, w->ph)) {
            Log::Warn("Falha ao capturar o monitor %d para o seletor", mons_[i].number);
        }
        wins_.push_back(std::move(w));
    }
    for (auto& w : wins_) {
        const RECT& r = mons_[(size_t)w->mon].rect;
        w->hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClass, L"Selecionar área", WS_POPUP, r.left, r.top,
                                  r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!w->hwnd) {
            Log::Error("Não foi possível criar a janela do seletor: %s", LastErrorText().c_str());
            DestroyAll();
            return false;
        }
    }
    for (auto& w : wins_) ShowWindow(w->hwnd, SW_SHOW);
    // foco na janela do monitor onde está o mouse (para Esc/Enter funcionarem)
    GetCursorPos(&mouse_);
    int mi = MonitorIndexForPoint(mons_, mouse_);
    HWND focus = wins_[(size_t)std::max(0, mi)]->hwnd;
    SetForegroundWindow(focus);
    SetFocus(focus);
    UpdateHover(mouse_);
    InvalidateAll();
    return true;
}

void RegionSelector::Cancel() {
    if (Active() && !finishing_) Finish(false);
}

RegionSelector::Win* RegionSelector::FindWin(HWND h) {
    for (auto& w : wins_)
        if (w->hwnd == h) return w.get();
    return nullptr;
}

LRESULT CALLBACK RegionSelector::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_sel) {
        if (Win* w = g_sel->FindWin(h)) return g_sel->Handle(*w, m, wp, lp);
    }
    return DefWindowProcW(h, m, wp, lp);
}

POINT RegionSelector::ScreenPt(HWND h, LPARAM lp) {
    POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ClientToScreen(h, &p);
    return p;
}

RECT RegionSelector::ClampToMonitor(RECT r, int mon) const {
    const RECT& m = mons_[(size_t)mon].rect;
    r.left = std::max(r.left, m.left);
    r.top = std::max(r.top, m.top);
    r.right = std::min(r.right, m.right);
    r.bottom = std::min(r.bottom, m.bottom);
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

static void HandlePoints(const RECT& s, POINT pts[8]) {
    LONG cx = (s.left + s.right) / 2, cy = (s.top + s.bottom) / 2;
    pts[0] = {s.left, s.top};    pts[1] = {cx, s.top};      pts[2] = {s.right, s.top};    pts[3] = {s.right, cy};
    pts[4] = {s.right, s.bottom}; pts[5] = {cx, s.bottom}; pts[6] = {s.left, s.bottom}; pts[7] = {s.left, cy};
}

int RegionSelector::HitHandle(POINT p) const {
    if (!hasSel_) return -1;
    POINT pts[8];
    HandlePoints(sel_, pts);
    LONG tol = (LONG)(9 * ScaleFor(selMon_));
    for (int i = 0; i < 8; i++)
        if (std::labs(p.x - pts[i].x) <= tol && std::labs(p.y - pts[i].y) <= tol) return i;
    return -1;
}

void RegionSelector::ButtonRects(RECT& rec, RECT& cancel, float s) const {
    LONG bh = (LONG)(38 * s), wRec = (LONG)(158 * s), wCan = (LONG)(112 * s), gap = (LONG)(8 * s), margin = (LONG)(10 * s);
    const RECT& m = mons_[(size_t)selMon_].rect;
    LONG top = sel_.bottom + margin;
    if (top + bh > m.bottom - margin) top = sel_.bottom - margin - bh;   // sem espaço embaixo: põe por dentro
    LONG right = std::min(sel_.right, m.right - margin);
    LONG left = right - (wRec + gap + wCan);
    if (left < m.left + margin) { left = m.left + margin; right = left + wRec + gap + wCan; }
    cancel = {left, top, left + wCan, top + bh};
    rec = {left + wCan + gap, top, right, top + bh};
}

int RegionSelector::HitButton(POINT p) const {
    if (!hasSel_ || drag_ != Drag::None) return 0;
    RECT rec, can;
    ButtonRects(rec, can, ScaleFor(selMon_));
    if (PtInRect(&rec, p)) return 1;
    if (PtInRect(&can, p)) return 2;
    return 0;
}

void RegionSelector::UpdateHover(POINT p) {
    hoverButton_ = HitButton(p);
    hoverValid_ = false;
    if (hasSel_ || drag_ != Drag::None) return;
    for (auto& c : candidates_) {
        if (PtInRect(&c.rect, p)) {
            hoverMon_ = MonitorIndexForPoint(mons_, p);
            hoverRect_ = ClampToMonitor(c.rect, hoverMon_);
            hoverValid_ = hoverRect_.right - hoverRect_.left >= 64 && hoverRect_.bottom - hoverRect_.top >= 64;
            return;
        }
    }
}

void RegionSelector::InvalidateAll() {
    for (auto& w : wins_) InvalidateRect(w->hwnd, nullptr, FALSE);
}

void RegionSelector::Finish(bool ok) {
    if (finishing_) return;
    if (ok) {
        // largura/altura pares (exigência dos codecs de vídeo)
        sel_.right = sel_.left + ((sel_.right - sel_.left) & ~1);
        sel_.bottom = sel_.top + ((sel_.bottom - sel_.top) & ~1);
    }
    finishing_ = true;
    resultOk_ = ok;
    Log::Info("Seleção de área %s: %ldx%ld em (%ld,%ld)", ok ? "confirmada" : "cancelada", sel_.right - sel_.left, sel_.bottom - sel_.top,
              sel_.left, sel_.top);
    for (auto& w : wins_) ShowWindow(w->hwnd, SW_HIDE);
    if (!wins_.empty()) PostMessageW(wins_[0]->hwnd, WM_APP_FINISH, 0, 0);
}

void RegionSelector::DestroyAll() {
    auto list = std::move(wins_);
    wins_.clear();
    for (auto& w : list) {
        SafeRelease(w->shot);
        w->canvas.Release();
        if (w->hwnd) DestroyWindow(w->hwnd);
    }
    if (g_sel == this) g_sel = nullptr;
}

LRESULT RegionSelector::Handle(Win& w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_FINISH: {
            Done cb = done_;
            bool ok = resultOk_;
            RECT r = sel_;
            int mon = selMon_;
            // As janelas já estão escondidas. Primeiro segue o fluxo (começa a
            // gravar), depois libera os recursos gráficos — liberar o Direct2D
            // pode demorar um pouco em alguns drivers e não precisa atrasar nada.
            auto old = std::move(wins_);
            wins_.clear();
            if (g_sel == this) g_sel = nullptr;
            if (cb) cb(ok, r, mon);
            for (auto& w : old) {
                SafeRelease(w->shot);
                w->canvas.Release();
                if (w->hwnd) DestroyWindow(w->hwnd);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(w.hwnd, &ps);
            Paint(w);
            EndPaint(w.hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR: {
            LPCWSTR cur = IDC_CROSS;
            int hh = HitHandle(mouse_);
            if (hoverButton_) cur = IDC_HAND;
            else if (drag_ == Drag::Move || (drag_ == Drag::None && hasSel_ && PtInRect(&sel_, mouse_) && hh < 0)) cur = IDC_SIZEALL;
            else {
                int h = drag_ == Drag::Resize ? resizeHandle_ : hh;
                if (h == 0 || h == 4) cur = IDC_SIZENWSE;
                else if (h == 2 || h == 6) cur = IDC_SIZENESW;
                else if (h == 1 || h == 5) cur = IDC_SIZENS;
                else if (h == 3 || h == 7) cur = IDC_SIZEWE;
            }
            SetCursor(LoadCursorW(nullptr, cur));
            return TRUE;
        }
        case WM_MOUSEMOVE: {
            POINT p = ScreenPt(w.hwnd, lp);
            mouse_ = p;
            if (drag_ == Drag::New) {
                RECT r = {std::min(dragStart_.x, p.x), std::min(dragStart_.y, p.y), std::max(dragStart_.x, p.x), std::max(dragStart_.y, p.y)};
                sel_ = ClampToMonitor(r, selMon_);
                hasSel_ = (sel_.right - sel_.left) >= 2 && (sel_.bottom - sel_.top) >= 2;
            } else if (drag_ == Drag::Move) {
                const RECT& m = mons_[(size_t)selMon_].rect;
                LONG wdt = dragOrig_.right - dragOrig_.left, hgt = dragOrig_.bottom - dragOrig_.top;
                LONG nx = std::clamp(dragOrig_.left + (p.x - dragStart_.x), m.left, m.right - wdt);
                LONG ny = std::clamp(dragOrig_.top + (p.y - dragStart_.y), m.top, m.bottom - hgt);
                sel_ = {nx, ny, nx + wdt, ny + hgt};
            } else if (drag_ == Drag::Resize) {
                RECT r = dragOrig_;
                LONG dx = p.x - dragStart_.x, dy = p.y - dragStart_.y;
                int h = resizeHandle_;
                if (h == 0 || h == 6 || h == 7) r.left += dx;
                if (h == 2 || h == 3 || h == 4) r.right += dx;
                if (h == 0 || h == 1 || h == 2) r.top += dy;
                if (h == 4 || h == 5 || h == 6) r.bottom += dy;
                RECT n = {std::min(r.left, r.right), std::min(r.top, r.bottom), std::max(r.left, r.right), std::max(r.top, r.bottom)};
                sel_ = ClampToMonitor(n, selMon_);
            } else {
                UpdateHover(p);
            }
            hoverButton_ = HitButton(p);
            InvalidateAll();
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p = ScreenPt(w.hwnd, lp);
            mouse_ = p;
            SetCapture(w.hwnd);
            int hh = HitHandle(p);
            if (HitButton(p)) {
                hoverButton_ = HitButton(p);
                return 0;                  // decide no soltar do botão
            } else if (hasSel_ && hh >= 0) {
                drag_ = Drag::Resize;
                resizeHandle_ = hh;
            } else if (hasSel_ && PtInRect(&sel_, p)) {
                drag_ = Drag::Move;
            } else {
                drag_ = Drag::New;
                selMon_ = std::max(0, MonitorIndexForPoint(mons_, p));
                hasSel_ = false;
            }
            dragStart_ = p;
            dragOrig_ = sel_;
            hint_ = kDefaultHint;
            InvalidateAll();
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p = ScreenPt(w.hwnd, lp);
            ReleaseCapture();
            if (drag_ == Drag::None) {
                int b = HitButton(p);
                if (b == 1) { Finish(true); return 0; }
                if (b == 2) { Finish(false); return 0; }
            }
            if (drag_ == Drag::New && (std::labs(p.x - dragStart_.x) < 5 && std::labs(p.y - dragStart_.y) < 5)) {
                // foi só um clique: seleciona a janela que estava sob o mouse
                hasSel_ = false;
                for (auto& c : candidates_) {
                    if (PtInRect(&c.rect, p)) {
                        selMon_ = std::max(0, MonitorIndexForPoint(mons_, p));
                        sel_ = ClampToMonitor(c.rect, selMon_);
                        hasSel_ = (sel_.right - sel_.left) >= 64 && (sel_.bottom - sel_.top) >= 64;
                        break;
                    }
                }
            }
            drag_ = Drag::None;
            hoverValid_ = false;
            hoverButton_ = HitButton(p);
            InvalidateAll();
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT p = ScreenPt(w.hwnd, lp);
            if (hasSel_ && PtInRect(&sel_, p)) {
                if ((sel_.right - sel_.left) >= 64 && (sel_.bottom - sel_.top) >= 64) Finish(true);
                else { hint_ = L"A área precisa ter pelo menos 64 × 64 pixels."; InvalidateAll(); }
            }
            return 0;
        }
        case WM_RBUTTONUP:
            if (drag_ != Drag::None) { drag_ = Drag::None; hasSel_ = false; ReleaseCapture(); }
            else if (hasSel_) hasSel_ = false;
            else Finish(false);
            InvalidateAll();
            return 0;
        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE) { Finish(false); return 0; }
            if (wp == VK_RETURN) {
                if (!hasSel_) hint_ = L"Primeiro desenhe uma área (ou clique numa janela).";
                else if ((sel_.right - sel_.left) < 64 || (sel_.bottom - sel_.top) < 64) hint_ = L"A área precisa ter pelo menos 64 × 64 pixels.";
                else { Finish(true); return 0; }
                InvalidateAll();
                return 0;
            }
            if (hasSel_ && (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN)) {
                LONG step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
                LONG dx = wp == VK_LEFT ? -step : (wp == VK_RIGHT ? step : 0);
                LONG dy = wp == VK_UP ? -step : (wp == VK_DOWN ? step : 0);
                const RECT& m = mons_[(size_t)selMon_].rect;
                LONG wdt = sel_.right - sel_.left, hgt = sel_.bottom - sel_.top;
                LONG nx = std::clamp(sel_.left + dx, m.left, m.right - wdt), ny = std::clamp(sel_.top + dy, m.top, m.bottom - hgt);
                sel_ = {nx, ny, nx + wdt, ny + hgt};
                InvalidateAll();
            }
            return 0;
        }
        case WM_CLOSE:
            Finish(false);
            return 0;
    }
    return DefWindowProcW(w.hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------
static void FillRR(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
    b->SetColor(c);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), b);
}

void RegionSelector::Paint(Win& w) {
    if (!w.canvas.Ensure(w.hwnd)) return;
    ID2D1HwndRenderTarget* rt = w.canvas.rt;
    if (!w.shot && !w.pixels.empty()) {
        rt->CreateBitmap(D2D1::SizeU((UINT)w.pw, (UINT)w.ph), w.pixels.data(), (UINT)w.pw * 4,
                         D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &w.shot);
    }
    const MonitorInfo& mon = mons_[(size_t)w.mon];
    float s = ScaleFor(w.mon);
    float ox = (float)mon.rect.left, oy = (float)mon.rect.top;
    float W = (float)mon.Width(), H = (float)mon.Height();
    auto L = [&](const RECT& r) { return D2D1::RectF(r.left - ox, r.top - oy, r.right - ox, r.bottom - oy); };
    ID2D1SolidColorBrush* b = w.canvas.Brush(Rgba(0, 1));

    rt->BeginDraw();
    rt->Clear(Rgba(0x000000));
    if (w.shot) rt->DrawBitmap(w.shot, D2D1::RectF(0, 0, W, H));
    D2D1_COLOR_F dim = Rgba(0x05070B, 0.58f);
    bool selHere = hasSel_ && selMon_ == w.mon;
    b->SetColor(dim);
    if (selHere) {
        D2D1_RECT_F sr = L(sel_);
        rt->FillRectangle(D2D1::RectF(0, 0, W, sr.top), b);
        rt->FillRectangle(D2D1::RectF(0, sr.bottom, W, H), b);
        rt->FillRectangle(D2D1::RectF(0, sr.top, sr.left, sr.bottom), b);
        rt->FillRectangle(D2D1::RectF(sr.right, sr.top, W, sr.bottom), b);
    } else {
        rt->FillRectangle(D2D1::RectF(0, 0, W, H), b);
    }

    IDWriteTextFormat* fSmall = MakeTextFormat(13.0f * s, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    IDWriteTextFormat* fBold = MakeTextFormat(14.0f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);

    // janela sob o mouse (antes de desenhar): moldura tracejada
    if (!hasSel_ && hoverValid_ && hoverMon_ == w.mon && drag_ == Drag::None) {
        D2D1_RECT_F hr = L(hoverRect_);
        b->SetColor(Rgba(Theme::Blue, 0.16f));
        rt->FillRectangle(hr, b);
        ID2D1StrokeStyle* dash = nullptr;
        D2DFactory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                                    D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DASH),
                                        nullptr, 0, &dash);
        b->SetColor(Rgba(Theme::Blue));
        rt->DrawRectangle(D2D1::RectF(hr.left + 1, hr.top + 1, hr.right - 1, hr.bottom - 1), b, 2.0f * s, dash);
        SafeRelease(dash);
    }

    if (selHere) {
        D2D1_RECT_F sr = L(sel_);
        b->SetColor(Rgba(Theme::Blue));
        rt->DrawRectangle(D2D1::RectF(sr.left - 1, sr.top - 1, sr.right + 1, sr.bottom + 1), b, 2.0f * s);
        // alças
        POINT pts[8];
        HandlePoints(sel_, pts);
        float hs = 4.5f * s;
        for (auto& p : pts) {
            D2D1_RECT_F hr = D2D1::RectF(p.x - ox - hs, p.y - oy - hs, p.x - ox + hs, p.y - oy + hs);
            b->SetColor(Rgba(0xFFFFFF));
            rt->FillRectangle(hr, b);
            b->SetColor(Rgba(Theme::Blue));
            rt->DrawRectangle(hr, b, 1.5f * s);
        }
        // tamanho da área
        int sw = (sel_.right - sel_.left) & ~1, sh = (sel_.bottom - sel_.top) & ~1;
        std::wstring dims = WFmt(L"%d × %d", sw, sh);
        float tw = TextWidth(fBold, dims) + 20 * s, th = 26 * s;
        float lx = sr.left, ly = sr.top - th - 6 * s;
        if (ly < 4 * s) ly = sr.top + 6 * s, lx = sr.left + 6 * s;
        FillRR(rt, b, D2D1::RectF(lx, ly, lx + tw, ly + th), 6 * s, Rgba(Theme::Panel, 0.95f));
        b->SetColor(Rgba(Theme::Text));
        rt->DrawText(dims.c_str(), (UINT32)dims.size(), fBold, D2D1::RectF(lx, ly, lx + tw, ly + th), b);

        // botões
        if (drag_ == Drag::None) {
            RECT recR, canR;
            ButtonRects(recR, canR, s);
            D2D1_RECT_F rr = L(recR), cr = L(canR);
            FillRR(rt, b, cr, 8 * s, Rgba(hoverButton_ == 2 ? 0x3A4252 : 0x2B3240, 0.97f));
            b->SetColor(Rgba(Theme::Text));
            rt->DrawText(L"Cancelar", 8, fBold, cr, b);
            FillRR(rt, b, rr, 8 * s, Rgba(hoverButton_ == 1 ? 0x2BD46A : Theme::GreenDark));
            // bolinha de gravação + texto
            float cy = (rr.top + rr.bottom) / 2;
            b->SetColor(Rgba(0xFFFFFF));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rr.left + 20 * s, cy), 5.5f * s, 5.5f * s), b);
            rt->DrawText(L"Gravar esta área", 16, fBold, D2D1::RectF(rr.left + 30 * s, rr.top, rr.right - 6 * s, rr.bottom), b);
        }
    }

    // faixa de instruções no topo
    {
        float tw = TextWidth(fSmall, hint_) + 36 * s, th = 34 * s;
        float lx = (W - tw) / 2, ly = 18 * s;
        FillRR(rt, b, D2D1::RectF(lx, ly, lx + tw, ly + th), 10 * s, Rgba(Theme::Panel, 0.94f));
        b->SetColor(Rgba(Theme::Border));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(lx, ly, lx + tw, ly + th), 10 * s, 10 * s), b, 1.0f);
        b->SetColor(Rgba(Theme::Text));
        rt->DrawText(hint_.c_str(), (UINT32)hint_.size(), fSmall, D2D1::RectF(lx, ly, lx + tw, ly + th), b);
    }
    SafeRelease(fSmall);
    SafeRelease(fBold);

    HRESULT hr = rt->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        SafeRelease(w.shot);
        w.canvas.Release();
        InvalidateRect(w.hwnd, nullptr, FALSE);
    }
}
