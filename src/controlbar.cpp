#include "controlbar.h"

#include <windowsx.h>
#include <algorithm>
#include <cmath>

#include "log.h"
#include "win_util.h"

static const wchar_t* kBarClass = L"SCP_ControlBar";
static const wchar_t* kBorderClass = L"SCP_RegionBorder";
static const wchar_t* kCountClass = L"SCP_Countdown";

static void RegisterOnce(const wchar_t* name, WNDPROC proc, HBRUSH bg) {
    WNDCLASSEXW wc = {sizeof(wc)};
    if (GetClassInfoExW(GetModuleHandleW(nullptr), name, &wc)) return;
    wc = {sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = bg;
    RegisterClassExW(&wc);
}

// ===========================================================================
// Barra de controle
// ===========================================================================
bool ControlBar::Show(const RECT& work, UINT dpi, bool withMic) {
    if (hwnd_) return true;
    RegisterOnce(kBarClass, WndProc, nullptr);
    s_ = dpi / 96.0f;
    withMic_ = withMic;
    micMuted_ = false;
    Layout();
    int margin = (int)(12 * s_);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kBarClass, APP_NAME_W L" — controles",
                            WS_POPUP, work.left + margin, work.top + margin, width_, height_, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        Log::Error("Não foi possível criar a barra flutuante: %s", LastErrorText().c_str());
        return false;
    }
    // O pulo do gato: a janela continua visível para você, mas some da captura
    // (DXGI, GDI e qualquer outro programa de gravação/print).
    excluded_ = ExcludeWindowFromCapture(hwnd_);
    Log::Info("Barra flutuante criada; exclusão da captura: %s", excluded_ ? "ativa" : "NÃO suportada neste Windows");
    SetWindowCorners(hwnd_, 2);
    if (GetWindowsVersion().build < 22000) {
        // Windows 10 não arredonda janelas sozinho: recorta os cantos
        HRGN rgn = CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, (int)(14 * s_), (int)(14 * s_));
        SetWindowRgn(hwnd_, rgn, FALSE);
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetTimer(hwnd_, 1, 100, nullptr);
    return true;
}

void ControlBar::Hide() {
    if (!hwnd_) return;
    KillTimer(hwnd_, 1);
    canvas_.Release();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

RECT ControlBar::WindowRect() const {
    RECT r{};
    if (hwnd_) GetWindowRect(hwnd_, &r);
    return r;
}

void ControlBar::MoveTo(int x, int y) {
    if (hwnd_) SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void ControlBar::Update(int state, double elapsed, bool hasSys, float sysLevel, bool hasMic, float micLevel, bool micMuted) {
    state_ = state;
    elapsed_ = elapsed;
    hasSys_ = hasSys;
    hasMic_ = hasMic;
    micMuted_ = micMuted;
    // os medidores caem devagar (fica mais legível)
    sysLevel_ = std::max(sysLevel, sysLevel_ * 0.80f);
    micLevel_ = std::max(micLevel, micLevel_ * 0.80f);
}

void ControlBar::Layout() {
    // mede os textos de verdade (a fonte pode variar de PC para PC)
    IDWriteTextFormat* fLabel = MakeTextFormat(12.5f * s_, DWRITE_FONT_WEIGHT_BOLD);
    IDWriteTextFormat* fTime = MakeTextFormat(16.0f * s_, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float lw = 0;
    for (const wchar_t* t : {L"REC", L"PAUSADO", L"INICIANDO", L"SALVANDO"}) lw = std::max(lw, TextWidth(fLabel, t));
    float tw = TextWidth(fTime, L"00:00:00");
    SafeRelease(fLabel);
    SafeRelease(fTime);
    if (lw <= 0) lw = 70 * s_;
    if (tw <= 0) tw = 70 * s_;
    labelX_ = 34 * s_;
    timeX_ = labelX_ + lw + 10 * s_;
    float x = timeX_ + tw + 14 * s_;
    sepX_ = x;
    x += 1 + 12 * s_;
    pauseBtn_ = D2D1::RectF(x, 7 * s_, x + 104 * s_, 37 * s_);
    x += (104 + 8) * s_;
    stopBtn_ = D2D1::RectF(x, 7 * s_, x + 104 * s_, 37 * s_);
    x += 104 * s_;
    if (withMic_) {
        // botão do microfone: "Mic" (ligado) / "Mudo" (silenciado)
        IDWriteTextFormat* fBtn = MakeTextFormat(13.0f * s_, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float tw = std::max(TextWidth(fBtn, L"Mic"), TextWidth(fBtn, L"Mudo"));
        SafeRelease(fBtn);
        if (tw <= 0) tw = 36 * s_;
        float bw = std::max(78 * s_, 30 * s_ + tw + 12 * s_);
        x += 8 * s_;
        micBtn_ = D2D1::RectF(x, 7 * s_, x + bw, 37 * s_);
        x += bw;
    } else {
        micBtn_ = D2D1::RectF(0, 0, 0, 0);
    }
    x += 12 * s_;
    x += (11 + 10) * s_;   // medidores
    x += (8 + 12) * s_;    // alça
    width_ = (int)x;
    height_ = (int)(44 * s_);
}

int ControlBar::HitButton(POINT p) const {
    D2D1_POINT_2F f = D2D1::Point2F((float)p.x, (float)p.y);
    auto in = [&](const D2D1_RECT_F& r) { return f.x >= r.left && f.x < r.right && f.y >= r.top && f.y < r.bottom; };
    if (in(pauseBtn_)) return 1;
    if (in(stopBtn_)) return 2;
    if (withMic_ && in(micBtn_)) return 3;
    return 0;
}

LRESULT CALLBACK ControlBar::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    ControlBar* self = reinterpret_cast<ControlBar*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = reinterpret_cast<ControlBar*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = h;
    }
    if (self && self->hwnd_ == h) return self->Handle(m, wp, lp);
    return DefWindowProcW(h, m, wp, lp);
}

LRESULT ControlBar::Handle(UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;   // clicar na barra não tira o foco do programa gravado
        case WM_NCHITTEST: {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &p);
            return HitButton(p) ? HTCLIENT : HTCAPTION;   // fora dos botões = arrasta a barra
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, hover_ ? IDC_HAND : IDC_ARROW));
            return TRUE;
        case WM_MOUSEMOVE: {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = HitButton(p);
            if (!tracking_) {
                TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&t);
                tracking_ = true;
            }
            if (h != hover_) { hover_ = h; InvalidateRect(hwnd_, nullptr, FALSE); }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            hover_ = 0;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            pressed_ = HitButton(p);
            if (pressed_) SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = HitButton(p);
            int was = pressed_;
            pressed_ = 0;
            ReleaseCapture();
            InvalidateRect(hwnd_, nullptr, FALSE);
            if (was && was == h && state_ != 3) {
                if (h == 1 && onPauseToggle) onPauseToggle();
                if (h == 2 && onStop) onStop();
                if (h == 3 && onMicToggle) onMicToggle();
            }
            return 0;
        }
        case WM_TIMER: {
            static int tick = 0;
            // de vez em quando reafirma "sempre por cima" (outros apps também usam topmost)
            if (++tick % 30 == 0) SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_DPICHANGED: {
            s_ = HIWORD(wp) / 96.0f;
            Layout();
            RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE);
            canvas_.Release();
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, m, wp, lp);
}

void ControlBar::Paint() {
    if (!canvas_.Ensure(hwnd_)) return;
    ID2D1HwndRenderTarget* rt = canvas_.rt;
    ID2D1SolidColorBrush* b = canvas_.Brush(Rgba(0));
    float s = s_, W = (float)width_, H = (float)height_;
    rt->BeginDraw();
    rt->Clear(Rgba(Theme::Panel));
    b->SetColor(Rgba(Theme::Border));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), 7 * s, 7 * s), b, 1.0f);

    float cy = H / 2;
    // bolinha: verde pulsando (gravando), âmbar (pausado), cinza (iniciando/processando)
    uint32_t dotColor = state_ == 1 ? Theme::Green : (state_ == 2 ? Theme::Amber : Theme::Muted);
    float alpha = 1.0f;
    if (state_ == 1) alpha = 0.55f + 0.45f * (float)std::cos(GetTickCount64() % 1200 / 1200.0 * 6.2831853);
    b->SetColor(Rgba(dotColor, 0.25f * alpha));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(20 * s, cy), 9 * s, 9 * s), b);
    b->SetColor(Rgba(dotColor, alpha));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(20 * s, cy), 5.5f * s, 5.5f * s), b);

    IDWriteTextFormat* fLabel = MakeTextFormat(12.5f * s, DWRITE_FONT_WEIGHT_BOLD);
    IDWriteTextFormat* fTime = MakeTextFormat(16.0f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    IDWriteTextFormat* fBtn = MakeTextFormat(13.0f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    const wchar_t* label = state_ == 1 ? L"REC" : state_ == 2 ? L"PAUSADO" : state_ == 3 ? L"SALVANDO" : L"INICIANDO";
    b->SetColor(Rgba(dotColor));
    rt->DrawText(label, (UINT32)wcslen(label), fLabel, D2D1::RectF(labelX_, 0, timeX_, H), b);
    std::wstring t = Widen(FormatDuration(elapsed_));
    b->SetColor(Rgba(Theme::Text));
    rt->DrawText(t.c_str(), (UINT32)t.size(), fTime, D2D1::RectF(timeX_, 0, sepX_, H), b);
    b->SetColor(Rgba(Theme::Border));
    rt->FillRectangle(D2D1::RectF(sepX_, 10 * s, sepX_ + 1, H - 10 * s), b);
    float x;

    auto button = [&](const D2D1_RECT_F& r, int id, uint32_t base, uint32_t hover, const wchar_t* text, int icon) {
        bool dis = state_ == 0 || state_ == 3;
        uint32_t c = pressed_ == id ? base : (hover_ == id ? hover : base);
        b->SetColor(Rgba(c, dis ? 0.45f : 1.0f));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6 * s, 6 * s), b);
        b->SetColor(Rgba(0xFFFFFF, dis ? 0.6f : 1.0f));
        float ix = r.left + 16 * s, iy = (r.top + r.bottom) / 2;
        if (icon == 0) {        // pausa: duas barras
            rt->FillRectangle(D2D1::RectF(ix - 5 * s, iy - 6 * s, ix - 2 * s, iy + 6 * s), b);
            rt->FillRectangle(D2D1::RectF(ix + 2 * s, iy - 6 * s, ix + 5 * s, iy + 6 * s), b);
        } else if (icon == 1) { // continuar: triângulo
            ID2D1PathGeometry* g = nullptr;
            D2DFactory()->CreatePathGeometry(&g);
            ID2D1GeometrySink* sk = nullptr;
            if (g && SUCCEEDED(g->Open(&sk))) {
                sk->BeginFigure(D2D1::Point2F(ix - 4 * s, iy - 6.5f * s), D2D1_FIGURE_BEGIN_FILLED);
                sk->AddLine(D2D1::Point2F(ix + 6 * s, iy));
                sk->AddLine(D2D1::Point2F(ix - 4 * s, iy + 6.5f * s));
                sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                sk->Close();
                sk->Release();
                rt->FillGeometry(g, b);
            }
            SafeRelease(g);
        } else {                // finalizar: quadrado
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ix - 5.5f * s, iy - 5.5f * s, ix + 5.5f * s, iy + 5.5f * s), 1.5f * s, 1.5f * s), b);
        }
        rt->DrawText(text, (UINT32)wcslen(text), fBtn, D2D1::RectF(r.left + 30 * s, r.top, r.right - 4 * s, r.bottom), b);
    };
    bool paused = state_ == 2;
    button(pauseBtn_, 1, Theme::BlueDark, Theme::Blue, paused ? L"Continuar" : L"Pausar", paused ? 1 : 0);
    button(stopBtn_, 2, Theme::RedDark, Theme::Red, L"Finalizar", 2);

    if (withMic_) {
        // microfone: cinza = ligado, âmbar com risco = silenciado
        const D2D1_RECT_F& r = micBtn_;
        bool dis = state_ == 3;
        uint32_t base = micMuted_ ? 0xB45309 : 0x334155, hov = micMuted_ ? 0xD97706 : 0x475569;
        b->SetColor(Rgba(hover_ == 3 && pressed_ != 3 ? hov : base, dis ? 0.45f : 1.0f));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6 * s, 6 * s), b);
        b->SetColor(Rgba(0xFFFFFF, dis ? 0.6f : 1.0f));
        float ix = r.left + 15 * s, iy = (r.top + r.bottom) / 2 - 0.5f * s;
        // cápsula
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ix - 3.2f * s, iy - 7.5f * s, ix + 3.2f * s, iy + 1.5f * s), 3.2f * s, 3.2f * s), b);
        // arco em volta da cápsula (meia-volta por baixo) e o pezinho
        ID2D1PathGeometry* g = nullptr;
        D2DFactory()->CreatePathGeometry(&g);
        ID2D1GeometrySink* sk = nullptr;
        if (g && SUCCEEDED(g->Open(&sk))) {
            float cr = 5.6f * s, cy0 = iy - 2.0f * s;
            sk->BeginFigure(D2D1::Point2F(ix + cr, cy0), D2D1_FIGURE_BEGIN_HOLLOW);
            for (int k = 1; k <= 12; k++) {
                float a = 3.14159265f * k / 12.0f;
                sk->AddLine(D2D1::Point2F(ix + cr * std::cos(a), cy0 + cr * std::sin(a)));
            }
            sk->EndFigure(D2D1_FIGURE_END_OPEN);
            sk->Close();
            sk->Release();
            rt->DrawGeometry(g, b, 1.6f * s);
        }
        SafeRelease(g);
        rt->DrawLine(D2D1::Point2F(ix, iy + 3.6f * s), D2D1::Point2F(ix, iy + 6.5f * s), b, 1.6f * s);
        rt->DrawLine(D2D1::Point2F(ix - 3.5f * s, iy + 6.8f * s), D2D1::Point2F(ix + 3.5f * s, iy + 6.8f * s), b, 1.6f * s);
        if (micMuted_) {
            // risco diagonal (com contorno da cor do fundo para destacar)
            b->SetColor(Rgba(base));
            rt->DrawLine(D2D1::Point2F(ix - 7 * s, iy - 8 * s), D2D1::Point2F(ix + 7 * s, iy + 8 * s), b, 3.6f * s);
            b->SetColor(Rgba(0xFFFFFF, dis ? 0.6f : 1.0f));
            rt->DrawLine(D2D1::Point2F(ix - 7 * s, iy - 8 * s), D2D1::Point2F(ix + 7 * s, iy + 8 * s), b, 1.7f * s);
        }
        const wchar_t* mt = micMuted_ ? L"Mudo" : L"Mic";
        rt->DrawText(mt, (UINT32)wcslen(mt), fBtn, D2D1::RectF(r.left + 28 * s, r.top, r.right - 4 * s, r.bottom), b);
    }

    // medidores de áudio (sistema / microfone)
    x = (withMic_ ? micBtn_.right : stopBtn_.right) + 12 * s;
    auto meter = [&](bool has, float lvl, bool muted) {
        float top = 11 * s, bot = H - 11 * s;
        b->SetColor(muted ? Rgba(Theme::Amber, 0.75f) : Rgba(Theme::Border));
        rt->FillRectangle(D2D1::RectF(x, top, x + 4 * s, bot), b);
        if (has && !muted) {
            float l = std::clamp(lvl, 0.0f, 1.0f);
            float y = bot - (bot - top) * std::sqrt(l);
            b->SetColor(Rgba(l > 0.9f ? Theme::Red : (l > 0.6f ? Theme::Amber : Theme::Green)));
            rt->FillRectangle(D2D1::RectF(x, y, x + 4 * s, bot), b);
        }
        x += 7 * s;
    };
    meter(hasSys_, sysLevel_, false);
    meter(hasMic_, micLevel_, micMuted_);
    // alça de arrastar (pontinhos)
    x += 7 * s;
    b->SetColor(Rgba(Theme::Muted, 0.8f));
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 2; c++)
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + c * 5 * s, cy - 6 * s + r * 6 * s), 1.4f * s, 1.4f * s), b);

    SafeRelease(fLabel);
    SafeRelease(fTime);
    SafeRelease(fBtn);
    if (rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) canvas_.Release();
}

// ===========================================================================
// Moldura da região
// ===========================================================================
LRESULT CALLBACK RegionBorder::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        bool paused = GetWindowLongPtrW(h, GWLP_USERDATA) != 0;
        HBRUSH br = CreateSolidBrush(paused ? RGB(0xF5, 0x9E, 0x0B) : RGB(0x22, 0xC5, 0x5E));
        FillRect(dc, &ps.rcPaint, br);
        DeleteObject(br);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, wp, lp);
}

void RegionBorder::Show(const RECT& r, UINT dpi) {
    Hide();
    RegisterOnce(kBorderClass, WndProc, nullptr);
    int t = std::max(2, (int)(2 * dpi / 96)), g = 1;   // espessura e folga (fica FORA da área gravada)
    RECT parts[4] = {
        {r.left - g - t, r.top - g - t, r.right + g + t, r.top - g},
        {r.left - g - t, r.bottom + g, r.right + g + t, r.bottom + g + t},
        {r.left - g - t, r.top - g, r.left - g, r.bottom + g},
        {r.right + g, r.top - g, r.right + g + t, r.bottom + g},
    };
    for (int i = 0; i < 4; i++) {
        const RECT& p = parts[i];
        parts_[i] = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                                    kBorderClass, L"", WS_POPUP, p.left, p.top, p.right - p.left, p.bottom - p.top, nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!parts_[i]) continue;
        SetLayeredWindowAttributes(parts_[i], 0, 230, LWA_ALPHA);   // clique atravessa a moldura
        SetWindowLongPtrW(parts_[i], GWLP_USERDATA, paused_ ? 1 : 0);
        ExcludeWindowFromCapture(parts_[i]);
        ShowWindow(parts_[i], SW_SHOWNOACTIVATE);
    }
}

void RegionBorder::SetPaused(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    for (HWND h : parts_)
        if (h) { SetWindowLongPtrW(h, GWLP_USERDATA, paused ? 1 : 0); InvalidateRect(h, nullptr, TRUE); }
}

void RegionBorder::Hide() {
    for (HWND& h : parts_)
        if (h) { DestroyWindow(h); h = nullptr; }
}

// ===========================================================================
// Contagem regressiva
// ===========================================================================
bool Countdown::Start(int seconds, const RECT& mon, UINT dpi, std::function<void(bool)> done) {
    if (hwnd_) return false;
    RegisterOnce(kCountClass, WndProc, nullptr);
    s_ = dpi / 96.0f;
    left_ = seconds;
    done_ = std::move(done);
    int w = (int)(240 * s_), h = (int)(220 * s_);
    int x = (mon.left + mon.right - w) / 2, y = (mon.top + mon.bottom - h) / 2;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kCountClass, L"Contagem regressiva", WS_POPUP, x, y, w, h, nullptr,
                            nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    ExcludeWindowFromCapture(hwnd_);
    SetWindowCorners(hwnd_, 2);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    SetTimer(hwnd_, 1, 1000, nullptr);
    return true;
}

void Countdown::Cancel() {
    if (hwnd_) Finish(false);
}

void Countdown::Finish(bool ok) {
    if (!hwnd_) return;
    KillTimer(hwnd_, 1);
    HWND h = hwnd_;
    hwnd_ = nullptr;
    ShowWindow(h, SW_HIDE);
    auto cb = std::move(done_);
    done_ = nullptr;
    if (cb) cb(ok);            // começa a gravar já...
    canvas_.Release();         // ...e só depois libera o Direct2D e a janela
    DestroyWindow(h);
}

LRESULT CALLBACK Countdown::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Countdown* self = reinterpret_cast<Countdown*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || self->hwnd_ != h) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_TIMER:
            if (--self->left_ <= 0) {
                // some ANTES do ffmpeg começar, para não sair no vídeo
                ShowWindow(h, SW_HIDE);
                self->Finish(true);
            } else {
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) self->Finish(false);
            return 0;
        case WM_RBUTTONUP:
            self->Finish(false);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            if (self->canvas_.Ensure(h)) {
                auto* rt = self->canvas_.rt;
                auto* b = self->canvas_.Brush(Rgba(0));
                RECT rc;
                GetClientRect(h, &rc);
                float W = (float)rc.right, H = (float)rc.bottom, s = self->s_;
                rt->BeginDraw();
                rt->Clear(Rgba(Theme::Panel));
                b->SetColor(Rgba(Theme::Border));
                rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), b, 1.0f);
                IDWriteTextFormat* f1 = MakeTextFormat(14 * s, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
                IDWriteTextFormat* f2 = MakeTextFormat(84 * s, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
                IDWriteTextFormat* f3 = MakeTextFormat(12 * s, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
                b->SetColor(Rgba(Theme::Muted));
                rt->DrawText(L"A gravação começa em", 20, f1, D2D1::RectF(0, 14 * s, W, 44 * s), b);
                std::wstring n = std::to_wstring(self->left_);
                b->SetColor(Rgba(Theme::Green));
                rt->DrawText(n.c_str(), (UINT32)n.size(), f2, D2D1::RectF(0, 40 * s, W, 170 * s), b);
                b->SetColor(Rgba(Theme::Muted));
                rt->DrawText(L"Esc para cancelar", 17, f3, D2D1::RectF(0, H - 40 * s, W, H - 12 * s), b);
                SafeRelease(f1);
                SafeRelease(f2);
                SafeRelease(f3);
                if (rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) self->canvas_.Release();
            }
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, m, wp, lp);
}
