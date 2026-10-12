#pragma once
#include "common.h"
#include "../third_party/imgui/imgui.h"

// ---------------------------------------------------------------------------
// Tema visual, fontes, ícones (desenhados com vetores, sem imagens) e os
// componentes próprios da interface (botões grandes, cartões, chaves...).
// ---------------------------------------------------------------------------

namespace UI {

// Paleta (grafite / azul-escuro)
constexpr uint32_t kBg = 0x11151C;
constexpr uint32_t kSurface = 0x161B23;
constexpr uint32_t kPanel = 0x1A1F29;
constexpr uint32_t kPanelHi = 0x212734;
constexpr uint32_t kBorder = 0x2B3240;
constexpr uint32_t kText = 0xE8ECF2;
constexpr uint32_t kMuted = 0x8C95A5;
constexpr uint32_t kFaint = 0x5E6676;
constexpr uint32_t kGreen = 0x22C55E;
constexpr uint32_t kGreenDark = 0x16A34A;
constexpr uint32_t kRed = 0xEF4444;
constexpr uint32_t kRedDark = 0xDC2626;
constexpr uint32_t kBlue = 0x3B82F6;
constexpr uint32_t kBlueDark = 0x2563EB;
constexpr uint32_t kAmber = 0xF59E0B;

inline ImU32 Col(uint32_t rgb, float a = 1.0f) {
    // multiplica pelo alpha global: assim os componentes próprios também ficam
    // "apagados" dentro de ImGui::BeginDisabled()
    if (ImGui::GetCurrentContext()) a *= ImGui::GetStyle().Alpha;
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(a * 255.0f));
}
inline ImVec4 ColV(uint32_t rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}
uint32_t Lighten(uint32_t rgb, float amount);

// Fontes
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
    ImFont* bold = nullptr;
};
Fonts& GetFonts();
bool LoadFonts();                     // Segoe UI do Windows; se faltar, Noto Sans embutida
void ApplyStyle(float dpiScale);      // tema + escala de DPI

// Atalhos de fonte (tamanhos em pixels "lógicos", a escala de DPI é automática)
void PushText(ImFont* f, float size);
inline void PopText() { ImGui::PopFont(); }

// Ícones vetoriais
enum class Icon {
    Record, Gear, Folder, Library, Monitor, Region, Mic, MicOff, Speaker, Disk, Pause, Play, Stop, Trash, Copy, Open, Back,
    Check, Close, Warning, Info, Refresh, Keyboard, Film, Gauge, Clock
};
void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// Componentes
float S(float v);                     // valor * escala de DPI
bool BigButton(const char* id, const char* label, const char* sub, Icon icon, uint32_t color, ImVec2 size, bool enabled = true);
bool ActionButton(const char* id, const char* label, Icon icon, uint32_t color, ImVec2 size, bool enabled = true);
bool GhostButton(const char* id, const char* label, Icon icon, ImVec2 size, bool enabled = true);
bool IconButton(const char* id, Icon icon, uint32_t color, const char* tooltip, float size = 0, bool danger = false);
bool MenuCard(const char* id, const char* title, const char* desc, Icon icon, uint32_t accent, ImVec2 size);
void InfoCard(const char* id, Icon icon, uint32_t accent, const char* title, const char* line1, const char* line2, ImVec2 size,
              float barFraction = -1.0f);
bool Toggle(const char* id, bool* v);
bool ToggleRow(const char* id, bool* v, const char* label);   // chave + texto alinhados
bool Segmented(const char* id, const char* const* items, int count, int* current, float width = 0);
void LevelMeter(const char* id, float level, ImVec2 size);
void StatusChip(const char* text, uint32_t color, bool pulse);
void SectionTitle(const char* text);
void HelpText(const char* text);
void Tooltip(const char* text);
bool ComboBox(const char* id, int* current, const std::vector<std::string>& items, float width = -1, const std::vector<bool>* enabled = nullptr);
void Spinner(ImVec2 center, float radius, ImU32 color);
void ProgressBarRounded(float fraction, ImVec2 size, uint32_t color);

}  // namespace UI
