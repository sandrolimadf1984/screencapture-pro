#include "d2d.h"

#include "log.h"

ID2D1Factory* D2DFactory() {
    static ID2D1Factory* f = [] {
        ID2D1Factory* x = nullptr;
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&x);
        if (FAILED(hr)) Log::Error("D2D1CreateFactory: %s", HResultText(hr).c_str());
        return x;
    }();
    return f;
}

IDWriteFactory* DWFactory() {
    static IDWriteFactory* f = [] {
        IDWriteFactory* x = nullptr;
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&x);
        if (FAILED(hr)) Log::Error("DWriteCreateFactory: %s", HResultText(hr).c_str());
        return x;
    }();
    return f;
}

IDWriteTextFormat* MakeTextFormat(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT palign) {
    IDWriteTextFormat* tf = nullptr;
    if (!DWFactory()) return nullptr;
    // "Segoe UI" existe em todo Windows 10/11; o DirectWrite troca por outra se faltar
    if (FAILED(DWFactory()->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             size, L"pt-br", &tf)))
        return nullptr;
    tf->SetTextAlignment(align);
    tf->SetParagraphAlignment(palign);
    tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return tf;
}

float TextWidth(IDWriteTextFormat* fmt, const std::wstring& text) {
    if (!fmt || !DWFactory()) return 0;
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(DWFactory()->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, 10000.0f, 1000.0f, &lay))) return 0;
    DWRITE_TEXT_METRICS m;
    lay->GetMetrics(&m);
    lay->Release();
    return m.widthIncludingTrailingWhitespace;
}

bool HwndCanvas::Ensure(HWND hwnd) {
    if (rt) return true;
    if (!D2DFactory()) return false;
    RECT rc;
    GetClientRect(hwnd, &rc);
    // DPI fixo em 96: 1 unidade do Direct2D = 1 pixel físico (fazemos a escala na mão)
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    HRESULT hr = D2DFactory()->CreateHwndRenderTarget(
        props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU((UINT)(rc.right - rc.left), (UINT)(rc.bottom - rc.top))), &rt);
    if (FAILED(hr)) {
        Log::Error("CreateHwndRenderTarget: %s", HResultText(hr).c_str());
        rt = nullptr;
        return false;
    }
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void HwndCanvas::Resize(UINT w, UINT h) {
    if (rt) rt->Resize(D2D1::SizeU(w, h));
}

void HwndCanvas::Release() {
    SafeRelease(brush);
    SafeRelease(rt);
}

ID2D1SolidColorBrush* HwndCanvas::Brush(D2D1_COLOR_F c) {
    if (!rt) return nullptr;
    if (!brush) rt->CreateSolidColorBrush(c, &brush);
    else brush->SetColor(c);
    return brush;
}
