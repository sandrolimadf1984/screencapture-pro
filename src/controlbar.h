#pragma once
#include "common.h"
#include "d2d.h"

// ---------------------------------------------------------------------------
// Barra flutuante "● REC 00:02:35 | ⏸ Pausar | ■ Finalizar | 🎤 Mic".
// Fica sempre por cima, não rouba o foco do programa que está sendo usado,
// pode ser arrastada e é EXCLUÍDA DA CAPTURA com SetWindowDisplayAffinity
// (WDA_EXCLUDEFROMCAPTURE) — ela aparece para você mas não sai no vídeo.
// ---------------------------------------------------------------------------
class ControlBar {
public:
    std::function<void()> onPauseToggle;
    std::function<void()> onStop;
    std::function<void()> onMicToggle;

    // workArea: área útil do monitor (posição padrão = canto superior esquerdo)
    // withMic: mostra o botão de silenciar o microfone
    bool Show(const RECT& workArea, UINT dpi, bool withMic);
    void Hide();
    bool Visible() const { return hwnd_ != nullptr; }
    bool ExcludedFromCapture() const { return excluded_; }
    HWND Hwnd() const { return hwnd_; }
    RECT WindowRect() const;
    void MoveTo(int x, int y);

    // estado: 0 = iniciando, 1 = gravando, 2 = pausado, 3 = processando
    void Update(int state, double elapsedSeconds, bool hasSys, float sysLevel, bool hasMic, float micLevel, bool micMuted);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Paint();
    void Layout();
    int HitButton(POINT clientPt) const;

    HWND hwnd_ = nullptr;
    HwndCanvas canvas_;
    float s_ = 1.0f;          // escala de DPI
    bool excluded_ = false;
    int state_ = 0;
    double elapsed_ = 0;
    bool hasSys_ = false, hasMic_ = false;
    float sysLevel_ = 0, micLevel_ = 0;
    int hover_ = 0, pressed_ = 0;
    D2D1_RECT_F pauseBtn_{}, stopBtn_{}, micBtn_{};
    bool withMic_ = false, micMuted_ = false;
    float labelX_ = 0, timeX_ = 0, sepX_ = 0;
    int width_ = 0, height_ = 0;
    bool tracking_ = false;
};

// Moldura fina em volta da área gravada (fica do lado de FORA da área, então
// nunca entra no vídeo; e ainda recebe a exclusão de captura por garantia).
class RegionBorder {
public:
    void Show(const RECT& region, UINT dpi);
    void SetPaused(bool paused);
    void Hide();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    HWND parts_[4] = {};
    bool paused_ = false;
};

// Contagem regressiva 3, 2, 1 antes de começar (Esc cancela)
class Countdown {
public:
    bool Start(int seconds, const RECT& monitorRect, UINT dpi, std::function<void(bool ok)> done);
    void Cancel();
    bool Active() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Finish(bool ok);
    HWND hwnd_ = nullptr;
    HwndCanvas canvas_;
    int left_ = 0;
    float s_ = 1.0f;
    std::function<void(bool)> done_;
};
