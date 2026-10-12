#include "ui.h"

#include <algorithm>
#include <cmath>

#include "log.h"
#include "resource.h"
#include "../third_party/imgui/imgui_internal.h"

namespace UI {

static float g_scale = 1.0f;
static Fonts g_fonts;

float S(float v) { return v * g_scale; }
Fonts& GetFonts() { return g_fonts; }

uint32_t Lighten(uint32_t rgb, float a) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    auto f = [&](int c) { return a >= 0 ? (int)(c + (255 - c) * a) : (int)(c * (1.0f + a)); };
    return (uint32_t)(std::clamp(f(r), 0, 255) << 16 | std::clamp(f(g), 0, 255) << 8 | std::clamp(f(b), 0, 255));
}

// ---------------------------------------------------------------------------
// Fontes
// ---------------------------------------------------------------------------
static bool LoadResourceFont(int id, ImFont** out) {
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!res) return false;
    HGLOBAL h = LoadResource(mod, res);
    void* data = h ? LockResource(h) : nullptr;
    DWORD size = SizeofResource(mod, res);
    if (!data || !size) return false;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;   // o recurso fica dentro do .exe (não pode ser liberado)
    *out = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, (int)size, 15.0f, &cfg);
    return *out != nullptr;
}

bool LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t winDir[MAX_PATH] = {0};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring fontsDir = JoinPath(winDir, L"Fonts");
    auto tryFile = [&](const wchar_t* name) -> ImFont* {
        std::wstring p = JoinPath(fontsDir, name);
        if (!FileExists(p)) return nullptr;
        return io.Fonts->AddFontFromFileTTF(Narrow(p).c_str(), 15.0f);
    };
    // Segoe UI é a fonte do próprio Windows: fica com cara de programa nativo
    g_fonts.regular = tryFile(L"segoeui.ttf");
    if (g_fonts.regular) {
        g_fonts.semibold = tryFile(L"seguisb.ttf");
        g_fonts.bold = tryFile(L"segoeuib.ttf");
    } else {
        Log::Info("Segoe UI não encontrada; usando a fonte embutida.");
        LoadResourceFont(IDR_FONT_REGULAR, &g_fonts.regular);
        LoadResourceFont(IDR_FONT_BOLD, &g_fonts.bold);
    }
    if (!g_fonts.regular) g_fonts.regular = io.Fonts->AddFontDefault();
    if (!g_fonts.bold) g_fonts.bold = g_fonts.semibold ? g_fonts.semibold : g_fonts.regular;
    if (!g_fonts.semibold) g_fonts.semibold = g_fonts.bold;
    io.FontDefault = g_fonts.regular;
    return true;
}

void PushText(ImFont* f, float size) { ImGui::PushFont(f ? f : g_fonts.regular, size); }

void ApplyStyle(float dpiScale) {
    g_scale = dpiScale;
    ImGuiStyle st;   // começa do zero (ScaleAllSizes não pode ser aplicado duas vezes)
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(22, 18);
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 12;
    st.GrabMinSize = 12;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 1;
    st.WindowRounding = 0;
    st.ChildRounding = 10;
    st.FrameRounding = 7;
    st.PopupRounding = 10;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 7;
    st.TabRounding = 7;
    st.CellPadding = ImVec2(8, 7);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ColV(kText);
    c[ImGuiCol_TextDisabled] = ColV(kFaint);
    c[ImGuiCol_WindowBg] = ColV(kBg);
    c[ImGuiCol_ChildBg] = ColV(kSurface);
    c[ImGuiCol_PopupBg] = ColV(kPanel);
    c[ImGuiCol_Border] = ColV(kBorder);
    c[ImGuiCol_BorderShadow] = ColV(0, 0);
    c[ImGuiCol_FrameBg] = ColV(0x1E2430);
    c[ImGuiCol_FrameBgHovered] = ColV(0x252C3A);
    c[ImGuiCol_FrameBgActive] = ColV(0x2B3343);
    c[ImGuiCol_TitleBg] = ColV(kPanel);
    c[ImGuiCol_TitleBgActive] = ColV(kPanel);
    c[ImGuiCol_MenuBarBg] = ColV(kPanel);
    c[ImGuiCol_ScrollbarBg] = ColV(0, 0);
    c[ImGuiCol_ScrollbarGrab] = ColV(0x2B3240);
    c[ImGuiCol_ScrollbarGrabHovered] = ColV(0x384154);
    c[ImGuiCol_ScrollbarGrabActive] = ColV(0x455066);
    c[ImGuiCol_CheckMark] = ColV(kBlue);
    c[ImGuiCol_SliderGrab] = ColV(kBlue);
    c[ImGuiCol_SliderGrabActive] = ColV(0x60A5FA);
    c[ImGuiCol_Button] = ColV(kBlueDark);
    c[ImGuiCol_ButtonHovered] = ColV(kBlue);
    c[ImGuiCol_ButtonActive] = ColV(0x1D4ED8);
    c[ImGuiCol_Header] = ColV(0x243049);
    c[ImGuiCol_HeaderHovered] = ColV(0x2C3A58);
    c[ImGuiCol_HeaderActive] = ColV(0x334369);
    c[ImGuiCol_Separator] = ColV(kBorder);
    c[ImGuiCol_SeparatorHovered] = ColV(kBlue);
    c[ImGuiCol_SeparatorActive] = ColV(kBlue);
    c[ImGuiCol_ResizeGrip] = ColV(0, 0);
    c[ImGuiCol_Tab] = ColV(kPanel);
    c[ImGuiCol_TabHovered] = ColV(0x2C3A58);
    c[ImGuiCol_TabSelected] = ColV(0x243049);
    c[ImGuiCol_TabSelectedOverline] = ColV(kBlue);
    c[ImGuiCol_TableHeaderBg] = ColV(kPanel);
    c[ImGuiCol_TableBorderStrong] = ColV(kBorder);
    c[ImGuiCol_TableBorderLight] = ColV(0x222834);
    c[ImGuiCol_TableRowBg] = ColV(0, 0);
    c[ImGuiCol_TableRowBgAlt] = ColV(0xFFFFFF, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ColV(kBlue, 0.35f);
    c[ImGuiCol_NavCursor] = ColV(kBlue);
    c[ImGuiCol_ModalWindowDimBg] = ColV(0x000000, 0.55f);
    st.ScaleAllSizes(dpiScale);
    st.FontScaleDpi = dpiScale;
    ImGui::GetStyle() = st;
}

// ---------------------------------------------------------------------------
// Ícones (tudo vetorial: fica nítido em qualquer escala de tela)
// ---------------------------------------------------------------------------
void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float sz, ImU32 col) {
    float t = std::max(1.5f, sz * 0.09f);   // espessura da linha
    float h = sz * 0.5f;
    auto P = [&](float x, float y) { return ImVec2(c.x + x * sz, c.y + y * sz); };
    switch (icon) {
        case Icon::Record:
            dl->AddCircle(c, h * 0.86f, col, 32, t);
            dl->AddCircleFilled(c, h * 0.52f, col, 32);
            break;
        case Icon::Gear: {
            for (int i = 0; i < 8; i++) {
                float a = i * 3.14159265f / 4.0f;
                dl->AddLine(ImVec2(c.x + cosf(a) * sz * 0.26f, c.y + sinf(a) * sz * 0.26f),
                            ImVec2(c.x + cosf(a) * sz * 0.45f, c.y + sinf(a) * sz * 0.45f), col, sz * 0.15f);
            }
            dl->AddCircle(c, sz * 0.27f, col, 24, sz * 0.12f);
            dl->AddCircle(c, sz * 0.10f, col, 16, t);
            break;
        }
        case Icon::Folder:
            dl->PathLineTo(P(-0.45f, -0.30f));
            dl->PathLineTo(P(-0.12f, -0.30f));
            dl->PathLineTo(P(-0.02f, -0.18f));
            dl->PathLineTo(P(0.45f, -0.18f));
            dl->PathLineTo(P(0.45f, 0.34f));
            dl->PathLineTo(P(-0.45f, 0.34f));
            dl->PathStroke(col, ImDrawFlags_Closed, t);
            dl->AddLine(P(-0.45f, -0.06f), P(0.45f, -0.06f), col, t);
            break;
        case Icon::Library:
            dl->AddRect(P(-0.42f, -0.32f), P(0.42f, 0.32f), col, sz * 0.08f, 0, t);
            dl->AddTriangleFilled(P(-0.10f, -0.15f), P(-0.10f, 0.15f), P(0.16f, 0.0f), col);
            break;
        case Icon::Film:
            dl->AddRect(P(-0.42f, -0.34f), P(0.42f, 0.34f), col, sz * 0.06f, 0, t);
            for (int i = 0; i < 4; i++) {
                float y = -0.24f + i * 0.16f;
                dl->AddRectFilled(P(-0.36f, y), P(-0.27f, y + 0.07f), col);
                dl->AddRectFilled(P(0.27f, y), P(0.36f, y + 0.07f), col);
            }
            break;
        case Icon::Monitor:
            dl->AddRect(P(-0.45f, -0.36f), P(0.45f, 0.20f), col, sz * 0.06f, 0, t);
            dl->AddLine(P(0.0f, 0.20f), P(0.0f, 0.36f), col, t);
            dl->AddLine(P(-0.20f, 0.38f), P(0.20f, 0.38f), col, t);
            break;
        case Icon::Region: {
            float a = 0.42f, l = 0.16f;
            dl->AddLine(P(-a, -a), P(-a + l, -a), col, t); dl->AddLine(P(-a, -a), P(-a, -a + l), col, t);
            dl->AddLine(P(a, -a), P(a - l, -a), col, t); dl->AddLine(P(a, -a), P(a, -a + l), col, t);
            dl->AddLine(P(-a, a), P(-a + l, a), col, t); dl->AddLine(P(-a, a), P(-a, a - l), col, t);
            dl->AddLine(P(a, a), P(a - l, a), col, t); dl->AddLine(P(a, a), P(a, a - l), col, t);
            dl->AddLine(P(-0.12f, 0), P(0.12f, 0), col, t);
            dl->AddLine(P(0, -0.12f), P(0, 0.12f), col, t);
            break;
        }
        case Icon::Mic:
        case Icon::MicOff:
            dl->AddRectFilled(P(-0.13f, -0.42f), P(0.13f, 0.10f), col, sz * 0.13f);
            dl->PathArcTo(P(0, -0.02f), sz * 0.26f, 0.0f, 3.14159265f, 16);
            dl->PathStroke(col, 0, t);
            dl->AddLine(P(0, 0.24f), P(0, 0.40f), col, t);
            dl->AddLine(P(-0.16f, 0.41f), P(0.16f, 0.41f), col, t);
            if (icon == Icon::MicOff) dl->AddLine(P(-0.40f, -0.44f), P(0.40f, 0.44f), col, t * 1.15f);   // risco = mudo
            break;
        case Icon::Speaker:
            dl->PathLineTo(P(-0.42f, -0.12f));
            dl->PathLineTo(P(-0.24f, -0.12f));
            dl->PathLineTo(P(-0.02f, -0.34f));
            dl->PathLineTo(P(-0.02f, 0.34f));
            dl->PathLineTo(P(-0.24f, 0.12f));
            dl->PathLineTo(P(-0.42f, 0.12f));
            dl->PathFillConvex(col);
            dl->PathArcTo(P(0.04f, 0), sz * 0.20f, -0.9f, 0.9f, 10);
            dl->PathStroke(col, 0, t);
            dl->PathArcTo(P(0.04f, 0), sz * 0.36f, -0.9f, 0.9f, 14);
            dl->PathStroke(col, 0, t);
            break;
        case Icon::Disk:
            dl->AddRect(P(-0.42f, -0.24f), P(0.42f, 0.24f), col, sz * 0.08f, 0, t);
            dl->AddLine(P(-0.42f, 0.04f), P(0.42f, 0.04f), col, t);
            dl->AddCircleFilled(P(0.26f, 0.14f), sz * 0.045f, col);
            break;
        case Icon::Pause:
            dl->AddRectFilled(P(-0.26f, -0.32f), P(-0.07f, 0.32f), col, sz * 0.04f);
            dl->AddRectFilled(P(0.07f, -0.32f), P(0.26f, 0.32f), col, sz * 0.04f);
            break;
        case Icon::Play:
            dl->AddTriangleFilled(P(-0.22f, -0.34f), P(-0.22f, 0.34f), P(0.34f, 0.0f), col);
            break;
        case Icon::Stop:
            dl->AddRectFilled(P(-0.27f, -0.27f), P(0.27f, 0.27f), col, sz * 0.06f);
            break;
        case Icon::Trash:
            dl->AddLine(P(-0.38f, -0.26f), P(0.38f, -0.26f), col, t);
            dl->AddLine(P(-0.12f, -0.36f), P(0.12f, -0.36f), col, t);
            dl->PathLineTo(P(-0.28f, -0.20f));
            dl->PathLineTo(P(-0.22f, 0.40f));
            dl->PathLineTo(P(0.22f, 0.40f));
            dl->PathLineTo(P(0.28f, -0.20f));
            dl->PathStroke(col, 0, t);
            dl->AddLine(P(-0.07f, -0.08f), P(-0.07f, 0.26f), col, t * 0.8f);
            dl->AddLine(P(0.07f, -0.08f), P(0.07f, 0.26f), col, t * 0.8f);
            break;
        case Icon::Copy:
            dl->AddRect(P(-0.36f, -0.20f), P(0.16f, 0.40f), col, sz * 0.06f, 0, t);
            dl->PathLineTo(P(-0.16f, -0.24f));
            dl->PathLineTo(P(-0.16f, -0.40f));
            dl->PathLineTo(P(0.38f, -0.40f));
            dl->PathLineTo(P(0.38f, 0.20f));
            dl->PathLineTo(P(0.22f, 0.20f));
            dl->PathStroke(col, 0, t);
            break;
        case Icon::Open:
            dl->PathLineTo(P(0.06f, -0.38f));
            dl->PathLineTo(P(-0.38f, -0.38f));
            dl->PathLineTo(P(-0.38f, 0.38f));
            dl->PathLineTo(P(0.38f, 0.38f));
            dl->PathLineTo(P(0.38f, -0.06f));
            dl->PathStroke(col, 0, t);
            dl->AddLine(P(-0.04f, 0.04f), P(0.40f, -0.40f), col, t);
            dl->AddLine(P(0.14f, -0.40f), P(0.40f, -0.40f), col, t);
            dl->AddLine(P(0.40f, -0.40f), P(0.40f, -0.14f), col, t);
            break;
        case Icon::Back:
            dl->AddLine(P(0.12f, -0.32f), P(-0.20f, 0.0f), col, t * 1.2f);
            dl->AddLine(P(-0.20f, 0.0f), P(0.12f, 0.32f), col, t * 1.2f);
            break;
        case Icon::Check:
            dl->AddLine(P(-0.32f, 0.02f), P(-0.08f, 0.26f), col, t * 1.3f);
            dl->AddLine(P(-0.08f, 0.26f), P(0.34f, -0.24f), col, t * 1.3f);
            break;
        case Icon::Close:
            dl->AddLine(P(-0.26f, -0.26f), P(0.26f, 0.26f), col, t * 1.2f);
            dl->AddLine(P(0.26f, -0.26f), P(-0.26f, 0.26f), col, t * 1.2f);
            break;
        case Icon::Warning:
            dl->AddTriangle(P(0, -0.40f), P(0.44f, 0.36f), P(-0.44f, 0.36f), col, t);
            dl->AddLine(P(0, -0.12f), P(0, 0.12f), col, t * 1.1f);
            dl->AddCircleFilled(P(0, 0.23f), t * 0.7f, col);
            break;
        case Icon::Info:
            dl->AddCircle(c, sz * 0.42f, col, 24, t);
            dl->AddLine(P(0, -0.04f), P(0, 0.22f), col, t * 1.1f);
            dl->AddCircleFilled(P(0, -0.18f), t * 0.7f, col);
            break;
        case Icon::Refresh:
            dl->PathArcTo(c, sz * 0.34f, 0.5f, 5.6f, 24);
            dl->PathStroke(col, 0, t);
            dl->AddTriangleFilled(P(0.40f, -0.30f), P(0.42f, 0.02f), P(0.12f, -0.10f), col);
            break;
        case Icon::Keyboard:
            dl->AddRect(P(-0.45f, -0.26f), P(0.45f, 0.26f), col, sz * 0.06f, 0, t);
            for (int r = 0; r < 2; r++)
                for (int k = 0; k < 4; k++)
                    dl->AddRectFilled(P(-0.33f + k * 0.18f, -0.15f + r * 0.14f), P(-0.25f + k * 0.18f, -0.08f + r * 0.14f), col);
            dl->AddLine(P(-0.22f, 0.15f), P(0.22f, 0.15f), col, t);
            break;
        case Icon::Gauge:
            dl->PathArcTo(P(0, 0.12f), sz * 0.40f, 3.14159265f, 6.2831853f, 20);
            dl->PathStroke(col, 0, t);
            dl->AddLine(P(0, 0.12f), P(0.22f, -0.12f), col, t * 1.2f);
            dl->AddCircleFilled(P(0, 0.12f), t * 1.2f, col);
            break;
        case Icon::Clock:
            dl->AddCircle(c, sz * 0.40f, col, 24, t);
            dl->AddLine(c, P(0, -0.24f), col, t);
            dl->AddLine(c, P(0.18f, 0.08f), col, t);
            break;
    }
}

// ---------------------------------------------------------------------------
// Componentes
// ---------------------------------------------------------------------------
static void HandCursorIfHovered(bool enabled) {
    if (ImGui::IsItemHovered() && enabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

bool BigButton(const char* id, const char* label, const char* sub, Icon icon, uint32_t color, ImVec2 size, bool enabled) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size) && enabled;
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    HandCursorIfHovered(enabled);
    uint32_t c = act ? Lighten(color, -0.12f) : (hov ? Lighten(color, 0.08f) : color);
    float r = S(14);
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, Col(c, enabled ? 1.0f : 0.35f), r);
    // brilho sutil no topo (degradê feito com faixas finas)
    for (int i = 0; i < 12; i++) {
        float y0 = p.y + size.y * 0.04f * i, y1 = y0 + size.y * 0.04f;
        dl->AddRectFilled(ImVec2(p.x + r * (i == 0 ? 0.6f : 0.0f), y0), ImVec2(q.x - r * (i == 0 ? 0.6f : 0.0f), y1),
                          Col(0xFFFFFF, 0.045f * (1.0f - i / 12.0f)), i == 0 ? r : 0.0f, i == 0 ? ImDrawFlags_RoundCornersTop : 0);
    }
    dl->AddRect(p, q, Col(0xFFFFFF, 0.10f), r);
    ImVec2 ic(p.x + S(44), p.y + size.y * 0.5f);
    dl->AddCircleFilled(ic, S(24), Col(0xFFFFFF, 0.18f), 40);
    DrawIcon(dl, icon, ic, S(26), Col(0xFFFFFF, enabled ? 1.0f : 0.6f));
    PushText(GetFonts().bold, 21);
    ImVec2 ls = ImGui::CalcTextSize(label);
    float textX = p.x + S(84);
    float subH = 0;
    if (sub && *sub) {
        PushText(GetFonts().regular, 13.5f);
        subH = ImGui::CalcTextSize(sub).y;
        PopText();
    }
    float total = ls.y + (subH > 0 ? subH + S(1) : 0);
    float y = p.y + (size.y - total) * 0.5f;
    dl->AddText(ImVec2(textX, y), Col(0xFFFFFF, enabled ? 1.0f : 0.7f), label);
    PopText();
    if (subH > 0) {
        PushText(GetFonts().regular, 13.5f);
        dl->AddText(ImVec2(textX, y + ls.y + S(1)), Col(0xFFFFFF, enabled ? 0.82f : 0.5f), sub);
        PopText();
    }
    return clicked;
}

bool ActionButton(const char* id, const char* label, Icon icon, uint32_t color, ImVec2 size, bool enabled) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size) && enabled;
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    HandCursorIfHovered(enabled);
    uint32_t c = act ? Lighten(color, -0.12f) : (hov ? Lighten(color, 0.10f) : color);
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, Col(c, enabled ? 1.0f : 0.35f), S(9));
    PushText(GetFonts().semibold, 14.5f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    float iconSz = S(17), gap = S(8);
    float w = ts.x + iconSz + gap;
    float x = p.x + (size.x - w) * 0.5f, cy = p.y + size.y * 0.5f;
    DrawIcon(dl, icon, ImVec2(x + iconSz * 0.5f, cy), iconSz, Col(0xFFFFFF, enabled ? 1.0f : 0.6f));
    dl->AddText(ImVec2(x + iconSz + gap, cy - ts.y * 0.5f), Col(0xFFFFFF, enabled ? 1.0f : 0.6f), label);
    PopText();
    return clicked;
}

bool GhostButton(const char* id, const char* label, Icon icon, ImVec2 size, bool enabled) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size) && enabled;
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    HandCursorIfHovered(enabled);
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, Col(act ? 0x2B3343 : (hov ? kPanelHi : kPanel)), S(9));
    dl->AddRect(p, q, Col(kBorder), S(9));
    PushText(GetFonts().semibold, 14.0f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    float iconSz = S(16), gap = S(8);
    float w = ts.x + iconSz + gap;
    float x = p.x + (size.x - w) * 0.5f, cy = p.y + size.y * 0.5f;
    ImU32 tc = Col(kText, enabled ? 1.0f : 0.45f);
    DrawIcon(dl, icon, ImVec2(x + iconSz * 0.5f, cy), iconSz, tc);
    dl->AddText(ImVec2(x + iconSz + gap, cy - ts.y * 0.5f), tc, label);
    PopText();
    return clicked;
}

bool IconButton(const char* id, Icon icon, uint32_t color, const char* tooltip, float size, bool danger) {
    if (size <= 0) size = S(30);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    HandCursorIfHovered(true);
    if (hov || act) dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), Col(danger ? kRed : 0xFFFFFF, act ? 0.22f : 0.10f), S(7));
    DrawIcon(dl, icon, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.56f, Col(hov && danger ? 0xFF7B7B : color));
    if (tooltip && hov) Tooltip(tooltip);
    return clicked;
}

bool MenuCard(const char* id, const char* title, const char* desc, Icon icon, uint32_t accent, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    HandCursorIfHovered(true);
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, Col(hov ? kPanelHi : kSurface), S(12));
    dl->AddRect(p, q, Col(hov ? accent : kBorder, hov ? 0.9f : 1.0f), S(12), 0, hov ? S(1.5f) : 1.0f);
    ImVec2 ic(p.x + S(36), p.y + size.y * 0.5f);
    dl->AddCircleFilled(ic, S(22), Col(accent, 0.16f), 40);
    DrawIcon(dl, icon, ic, S(24), Col(accent));
    PushText(GetFonts().semibold, 16.5f);
    ImVec2 ts = ImGui::CalcTextSize(title);
    PopText();
    PushText(GetFonts().regular, 13.0f);
    float wrap = size.x - S(80) - S(14);
    ImVec2 ds = ImGui::CalcTextSize(desc, nullptr, false, wrap);
    PopText();
    float y = p.y + (size.y - ts.y - ds.y - S(3)) * 0.5f;
    PushText(GetFonts().semibold, 16.5f);
    dl->AddText(ImVec2(p.x + S(72), y), Col(kText), title);
    PopText();
    PushText(GetFonts().regular, 13.0f);
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + S(72), y + ts.y + S(3)), Col(kMuted), desc, nullptr, wrap);
    PopText();
    return clicked;
}

// Encurta o texto com "…" no meio para caber na largura (mantém o começo e o
// fim — bom para caminhos de pasta). Respeita os caracteres UTF-8 acentuados.
static std::string FitMiddle(const char* text, float maxW) {
    std::string s = text ? text : "";
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    std::vector<size_t> starts;
    for (size_t i = 0; i < s.size(); i++)
        if ((s[i] & 0xC0) != 0x80) starts.push_back(i);
    size_t n = starts.size();
    for (size_t keep = n > 0 ? n - 1 : 0; keep > 4; keep--) {
        size_t head = keep * 2 / 5, tail = keep - head;
        std::string t = s.substr(0, starts[head]) + "…" + s.substr(starts[n - tail]);
        if (ImGui::CalcTextSize(t.c_str()).x <= maxW) return t;
    }
    return "…";
}

void InfoCard(const char* id, Icon icon, uint32_t accent, const char* title, const char* line1, const char* line2, ImVec2 size,
              float bar) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, Col(kSurface), S(12));
    dl->AddRect(p, q, Col(kBorder), S(12));
    ImVec2 ic(p.x + S(28), p.y + S(30));
    dl->AddCircleFilled(ic, S(16), Col(accent, 0.15f), 32);
    DrawIcon(dl, icon, ic, S(18), Col(accent));
    float x = p.x + S(54), w = size.x - S(54) - S(12);
    PushText(GetFonts().semibold, 11.5f);
    dl->AddText(ImVec2(x, p.y + S(12)), Col(kMuted), title);
    PopText();
    ImGui::PushClipRect(ImVec2(x, p.y), ImVec2(x + w, q.y), true);
    PushText(GetFonts().semibold, 15.0f);
    dl->AddText(ImVec2(x, p.y + S(29)), Col(kText), FitMiddle(line1, w).c_str());
    PopText();
    if (line2 && *line2) {
        PushText(GetFonts().regular, 12.5f);
        dl->AddText(ImVec2(x, p.y + S(50)), Col(kMuted), FitMiddle(line2, w).c_str());
        PopText();
    }
    ImGui::PopClipRect();
    if (bar >= 0.0f) {
        float by = q.y - S(14), bx = x;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + w, by + S(5)), Col(kBorder), S(3));
        uint32_t bc = bar > 0.92f ? kRed : (bar > 0.80f ? kAmber : kBlue);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + w * std::clamp(bar, 0.02f, 1.0f), by + S(5)), Col(bc), S(3));
    }
    (void)id;
}

bool Toggle(const char* id, bool* v) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = S(40), h = S(22);
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (clicked) *v = !*v;
    HandCursorIfHovered(true);
    bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(*v ? (hov ? kBlue : kBlueDark) : (hov ? 0x3A4252 : 0x2E3543)), h * 0.5f);
    float r = h * 0.5f - S(3);
    float cx = *v ? p.x + w - h * 0.5f : p.x + h * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, Col(0xFFFFFF), 24);
    return clicked;
}

bool ToggleRow(const char* id, bool* v, const char* label) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool c = Toggle(id, v);
    ImGui::SameLine(0, S(10));
    float th = ImGui::GetTextLineHeight();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + (S(22) - th) * 0.5f));
    ImGui::TextUnformatted(label);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + S(22) + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));
    return c;
}

bool Segmented(const char* id, const char* const* items, int count, int* current, float width) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    float h = S(34), pad = S(3);
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), Col(0x1E2430), S(9));
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), Col(kBorder), S(9));
    float segW = (width - pad * 2) / count;
    bool changed = false;
    for (int i = 0; i < count; i++) {
        ImVec2 a(p.x + pad + segW * i, p.y + pad), b(a.x + segW, p.y + h - pad);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(segW, h - pad * 2))) {
            if (*current != i) changed = true;
            *current = i;
        }
        HandCursorIfHovered(true);
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (*current == i) dl->AddRectFilled(a, b, Col(kBlueDark), S(7));
        else if (hov) dl->AddRectFilled(a, b, Col(0xFFFFFF, 0.06f), S(7));
        PushText(GetFonts().semibold, 13.5f);
        ImVec2 ts = ImGui::CalcTextSize(items[i]);
        dl->AddText(ImVec2(a.x + (segW - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f), Col(*current == i ? 0xFFFFFF : kMuted), items[i]);
        PopText();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(width, 0));
    ImGui::PopID();
    return changed;
}

void LevelMeter(const char* id, float level, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(0x1E2430), size.y * 0.5f);
    // escala perceptual (raiz quadrada ~ próxima do dB visualmente)
    float l = std::sqrt(std::clamp(level, 0.0f, 1.0f));
    int segs = 24;
    float gap = S(2), segW = (size.x - gap * (segs - 1)) / segs;
    for (int i = 0; i < segs; i++) {
        float f = (i + 1) / (float)segs;
        if (f > l + 0.001f) break;
        uint32_t c = f > 0.92f ? kRed : (f > 0.75f ? kAmber : kGreen);
        float x = p.x + i * (segW + gap);
        dl->AddRectFilled(ImVec2(x, p.y + S(2)), ImVec2(x + segW, p.y + size.y - S(2)), Col(c), S(1.5f));
    }
    (void)id;
}

void StatusChip(const char* text, uint32_t color, bool pulse) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    PushText(GetFonts().semibold, 13.0f);
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = S(28), w = ts.x + S(36);
    ImGui::Dummy(ImVec2(w, h));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(color, 0.14f), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Col(color, 0.45f), h * 0.5f);
    float a = 1.0f;
    if (pulse) a = 0.5f + 0.5f * (float)std::cos(ImGui::GetTime() * 5.0);
    ImVec2 dc(p.x + S(15), p.y + h * 0.5f);
    dl->AddCircleFilled(dc, S(4.5f), Col(color, a), 16);
    dl->AddText(ImVec2(p.x + S(26), p.y + (h - ts.y) * 0.5f), Col(Lighten(color, 0.35f)), text);
    PopText();
}

void SectionTitle(const char* text) {
    PushText(GetFonts().semibold, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ColV(kMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    PopText();
}

void HelpText(const char* text) {
    PushText(GetFonts().regular, 12.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ColV(kMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    PopText();
}

void Tooltip(const char* text) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(7)));
    if (ImGui::BeginTooltip()) {
        PushText(GetFonts().regular, 13.0f);
        ImGui::PushTextWrapPos(S(320));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        PopText();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

bool ComboBox(const char* id, int* current, const std::vector<std::string>& items, float width, const std::vector<bool>* enabled) {
    bool changed = false;
    if (width > 0) ImGui::SetNextItemWidth(width);
    else if (width < 0) ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const char* preview = (*current >= 0 && *current < (int)items.size()) ? items[(size_t)*current].c_str() : "";
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(6), S(6)));
    ImGui::PushStyleColor(ImGuiCol_Button, ColV(0x2B3343));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ColV(0x384154));
    bool opened = ImGui::BeginCombo(id, preview);
    ImGui::PopStyleColor(2);
    if (opened) {
        for (int i = 0; i < (int)items.size(); i++) {
            bool en = !enabled || (size_t)i >= enabled->size() || (*enabled)[(size_t)i];
            ImGui::BeginDisabled(!en);
            bool sel = i == *current;
            if (ImGui::Selectable(items[(size_t)i].c_str(), sel) && en) {
                if (*current != i) changed = true;
                *current = i;
            }
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    return changed;
}

void Spinner(ImVec2 c, float r, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = (float)ImGui::GetTime() * 6.0f;
    dl->PathArcTo(c, r, t, t + 4.2f, 24);
    dl->PathStroke(col, 0, std::max(2.0f, r * 0.25f));
}

void ProgressBarRounded(float f, ImVec2 size, uint32_t color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(0x1E2430), size.y * 0.5f);
    float w = size.x * std::clamp(f, 0.0f, 1.0f);
    if (w > size.y) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + size.y), Col(color), size.y * 0.5f);
}

}  // namespace UI
