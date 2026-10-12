#pragma once
#include "common.h"
#include "d2d.h"
#include "monitors.h"

// ---------------------------------------------------------------------------
// Seletor de área ("Gravação livre").
// Cobre cada monitor com uma janela que mostra a tela "congelada" e escurecida.
// O usuário arrasta para desenhar o retângulo (ou clica numa janela para
// selecioná-la), ajusta pelas alças, e confirma com o botão "Gravar área",
// Enter ou duplo clique. Esc ou o botão direito cancelam.
// Tudo em pixels físicos, então funciona com qualquer escala do Windows e
// com monitores de DPIs diferentes.
// ---------------------------------------------------------------------------
class RegionSelector {
public:
    using Done = std::function<void(bool ok, RECT region, int monitorIndex)>;
    bool Begin(const std::vector<MonitorInfo>& monitors, const std::vector<HWND>& ignoreWindows, Done done);
    bool Active() const { return !wins_.empty(); }
    void Cancel();

private:
    struct Win {
        HWND hwnd = nullptr;
        int mon = 0;
        HwndCanvas canvas;
        ID2D1Bitmap* shot = nullptr;
        std::vector<uint8_t> pixels;   // captura em BGRA
        int pw = 0, ph = 0;
    };
    struct Candidate { RECT rect; };
    enum class Drag { None, New, Move, Resize };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(Win& w, UINT msg, WPARAM wp, LPARAM lp);
    void Paint(Win& w);
    void InvalidateAll();
    void Finish(bool ok);
    void DestroyAll();
    Win* FindWin(HWND h);
    POINT ScreenPt(HWND h, LPARAM lp);
    int HitHandle(POINT p) const;       // -1 nenhuma, 0..7 alças
    int HitButton(POINT p) const;       // 0 nada, 1 gravar, 2 cancelar
    void ButtonRects(RECT& rec, RECT& cancel, float scale) const;
    RECT ClampToMonitor(RECT r, int mon) const;
    void UpdateHover(POINT p);
    float ScaleFor(int mon) const { return mons_[(size_t)mon].dpi / 96.0f; }

    std::vector<MonitorInfo> mons_;
    std::vector<std::unique_ptr<Win>> wins_;
    std::vector<Candidate> candidates_;
    Done done_;
    bool hasSel_ = false;
    RECT sel_{};
    int selMon_ = 0;
    Drag drag_ = Drag::None;
    int resizeHandle_ = -1;
    POINT dragStart_{};
    RECT dragOrig_{};
    bool hoverValid_ = false;
    RECT hoverRect_{};
    int hoverMon_ = 0;
    int hoverButton_ = 0;
    POINT mouse_{};
    std::wstring hint_;
    bool finishing_ = false;
    bool resultOk_ = false;
    IDWriteTextFormat* fmtSmall_ = nullptr;
    IDWriteTextFormat* fmtBold_ = nullptr;
    float fmtScale_ = 0;
};
