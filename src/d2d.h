#pragma once
#include <d2d1.h>
#include <dwrite.h>

#include "common.h"

// Pequenas ajudas para Direct2D/DirectWrite (usados nas janelas que ficam por
// cima da tela: seletor de área, barra flutuante, contagem regressiva).
ID2D1Factory* D2DFactory();
IDWriteFactory* DWFactory();
IDWriteTextFormat* MakeTextFormat(float sizePx, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                                  DWRITE_PARAGRAPH_ALIGNMENT palign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
// Mede a largura de um texto
float TextWidth(IDWriteTextFormat* fmt, const std::wstring& text);

inline D2D1_COLOR_F Rgba(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

// Cores do tema (as mesmas da janela principal)
namespace Theme {
constexpr uint32_t Bg = 0x11151C;
constexpr uint32_t Panel = 0x1A1F29;
constexpr uint32_t Border = 0x2B3240;
constexpr uint32_t Text = 0xE8ECF2;
constexpr uint32_t Muted = 0x8C95A5;
constexpr uint32_t Green = 0x22C55E;
constexpr uint32_t GreenDark = 0x16A34A;
constexpr uint32_t Red = 0xEF4444;
constexpr uint32_t RedDark = 0xDC2626;
constexpr uint32_t Blue = 0x3B82F6;
constexpr uint32_t BlueDark = 0x2563EB;
constexpr uint32_t Amber = 0xF59E0B;
}  // namespace Theme

// Render target simples ligado a uma janela (recriado se o dispositivo for perdido)
struct HwndCanvas {
    ID2D1HwndRenderTarget* rt = nullptr;
    bool Ensure(HWND hwnd);
    void Resize(UINT w, UINT h);
    void Release();
    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F c);   // pincel reaproveitado
    ID2D1SolidColorBrush* brush = nullptr;
};
