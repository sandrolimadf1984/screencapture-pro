// Telas da janela principal (desenhadas com Dear ImGui a cada quadro).
#include <algorithm>
#include <cmath>

#include "app.h"
#include "finalize.h"
#include "log.h"
#include "paths.h"
#include "process.h"
#include "ui.h"
#include "win_util.h"
#include "../third_party/imgui/imgui_internal.h"

using namespace UI;

static const char* kTabNames[] = {"Vídeo", "Áudio", "Gravações", "Atalhos", "Avançado"};

// ---------------------------------------------------------------------------
// Raiz
// ---------------------------------------------------------------------------
void App::DrawRoot() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##root", nullptr, f);
    // se a tela do áudio foi deixada, desliga os medidores
    if (!(page_ == Page::Settings && settingsTab_ == 1) && (monSys_ || monMic_)) StopMonitoring();
    switch (page_) {
        case Page::Home: DrawHome(); break;
        case Page::Monitors: DrawMonitors(); break;
        case Page::Settings: DrawSettings(); break;
        case Page::Library: DrawLibrary(); break;
    }
    DrawModals();
    DrawToast();
    ImGui::End();
}

static void DrawLogo(ImDrawList* dl, ImVec2 p, float sz) {
    ImVec2 q(p.x + sz, p.y + sz);
    dl->AddRectFilled(p, q, Col(kBlueDark), sz * 0.26f);
    dl->AddRectFilled(p, ImVec2(q.x, p.y + sz * 0.5f), Col(0xFFFFFF, 0.08f), sz * 0.26f, ImDrawFlags_RoundCornersTop);
    ImVec2 c(p.x + sz * 0.5f, p.y + sz * 0.52f);
    DrawIcon(dl, Icon::Monitor, c, sz * 0.62f, Col(0xFFFFFF));
    dl->AddCircleFilled(ImVec2(q.x - sz * 0.18f, p.y + sz * 0.18f), sz * 0.15f, Col(kBg), 20);
    dl->AddCircleFilled(ImVec2(q.x - sz * 0.18f, p.y + sz * 0.18f), sz * 0.11f, Col(kRed), 20);
}

void App::DrawHeader(const char* title, bool back) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 start = ImGui::GetCursorScreenPos();
    if (back) {
        if (IconButton("##back", Icon::Back, kText, "Voltar", S(34))) {
            if (page_ == Page::Settings) StopMonitoring();
            capturingHotkey_ = 0;
            page_ = Page::Home;
            ApplyHotkeys(false);
        }
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + S(4), start.y + S(2)));
        PushText(GetFonts().bold, 22);
        ImGui::TextUnformatted(title);
        PopText();
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + S(46)));
    } else {
        DrawLogo(dl, start, S(44));
        PushText(GetFonts().bold, 21);
        dl->AddText(ImVec2(start.x + S(58), start.y + S(2)), Col(kText), APP_NAME_U8);
        PopText();
        PushText(GetFonts().regular, 13);
        dl->AddText(ImVec2(start.x + S(58), start.y + S(27)), Col(kMuted), APP_SUBTITLE_U8 " para Windows");
        PopText();
        // estado atual à direita
        RecState st = recorder_->State();
        const char* txt = "Pronto";
        uint32_t c = kBlue;
        bool pulse = false;
        if (!capsReady_) { txt = "Preparando…"; c = kMuted; }
        if (st == RecState::Starting || st == RecState::Resuming) { txt = "Iniciando…"; c = kBlue; pulse = true; }
        else if (st == RecState::Recording) { txt = "Gravando"; c = kGreen; pulse = true; }
        else if (st == RecState::Paused || st == RecState::Pausing) { txt = "Pausado"; c = kAmber; }
        else if (finalizing_ || st == RecState::Stopping || st == RecState::Finalizing) { txt = "Processando…"; c = kBlue; pulse = true; }
        else if (!notices_.empty() && notices_.front().kind == Notice::Kind::Success && ImGui::GetTime() - notices_.front().time < 12)
            { txt = "Concluído"; c = kGreen; }
        PushText(GetFonts().semibold, 13.0f);
        float w = ImGui::CalcTextSize(txt).x + S(36);
        PopText();
        ImGui::SetCursorScreenPos(ImVec2(start.x + avail - w, start.y + S(8)));
        StatusChip(txt, c, pulse);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + S(58)));
    }
    ImGui::Dummy(ImVec2(0, 0));
}

// ---------------------------------------------------------------------------
// Tela inicial
// ---------------------------------------------------------------------------
void App::DrawHome() {
    DrawHeader(nullptr, false);
    float W = ImGui::GetContentRegionAvail().x;
    RecState st = recorder_->State();
    bool busy = recorder_->IsBusy() && st != RecState::Finalizing;

    if (busy) {
        DrawRecordPanel();
    } else if (finalizing_) {
        ImGui::BeginChild("##proc", ImVec2(W, S(118)), ImGuiChildFlags_Borders);
        ImVec2 p = ImGui::GetCursorScreenPos();
        Spinner(ImVec2(p.x + S(16), p.y + S(18)), S(11), Col(kBlue));
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(38), p.y + S(5)));
        PushText(GetFonts().semibold, 17);
        ImGui::Text("%s… %d%%", recovering_ ? "Recuperando a gravação" : "Processando o vídeo", (int)(finalizeProgress_ * 100));
        PopText();
        ImGui::Spacing();
        ProgressBarRounded(finalizeProgress_, ImVec2(ImGui::GetContentRegionAvail().x, S(10)), kBlue);
        HelpText(finalizeStage_.c_str());
        ImGui::EndChild();
    } else {
        if (BigButton("##rec", "Gravar", "Tela inteira ou uma área livre", Icon::Record, kGreenDark, ImVec2(W, S(84)), capsReady_ || true)) {
            ImGui::OpenPopup("##recmenu");
        }
        if (openRecordMenu_) {
            ImGui::OpenPopup("##recmenu");
            openRecordMenu_ = false;
        }
        ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(bmin.x, bmax.y + S(6)));
        ImGui::SetNextWindowSize(ImVec2(W, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(10)));
        if (ImGui::BeginPopup("##recmenu")) {
            float cw = ImGui::GetContentRegionAvail().x;
            if (MenuCard("##full", "Gravar tela inteira", "Grava um monitor inteiro. Você escolhe qual e confirma antes de começar.",
                         Icon::Monitor, kBlue, ImVec2(cw, S(76)))) {
                ImGui::CloseCurrentPopup();
                PostUi([this] { GoFullScreenPicker(); });
            }
            if (MenuCard("##free", "Gravação livre", "Desenhe na tela a área que quer gravar (ou clique numa janela). Grava só aquela parte.",
                         Icon::Region, kGreen, ImVec2(cw, S(76)))) {
                ImGui::CloseCurrentPopup();
                PostUi([this] { BeginRegionSelection(); });
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }

    ImGui::Dummy(ImVec2(0, S(2)));
    // ---- ações secundárias (azul)
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float bw = (W - gap * 2) / 3.0f;
    if (ActionButton("##cfg", "Configurações", Icon::Gear, kBlueDark, ImVec2(bw, S(44)))) OpenSettings(0);
    ImGui::SameLine();
    if (ActionButton("##folder", "Abrir pasta", Icon::Folder, kBlueDark, ImVec2(bw, S(44)))) {
        EnsureDir(OutputDir());
        OpenFolderInExplorer(OutputDir());
    }
    ImGui::SameLine();
    if (ActionButton("##lib", "Ver gravações", Icon::Library, kBlueDark, ImVec2(bw, S(44)))) {
        library_.Refresh(OutputDir());
        page_ = Page::Library;
    }

    // ---- indicadores
    ImGui::Dummy(ImVec2(0, S(2)));
    float cw = (W - gap) / 2.0f, ch = S(78);
    EncoderCaps caps = GetEncoderCaps();
    std::string fmt1 = std::string(ContainerName(settings_.container)) + " · " + CodecName(settings_.CurrentCodec());
    std::string fmt2 = settings_.container == Container::MP4 ? Fmt("Áudio AAC %d kbps", settings_.aacBitrate)
                       : settings_.aviAudio == AviAudio::PCM ? std::string("Áudio PCM (sem compressão)")
                                                             : Fmt("Áudio MP3 %d kbps", settings_.mp3Bitrate);
    if (!settings_.sysAudio && !settings_.mic) fmt2 = "Sem áudio";
    InfoCard("##c1", Icon::Film, kBlue, "FORMATO", fmt1.c_str(), fmt2.c_str(), ImVec2(cw, ch));
    ImGui::SameLine();
    std::string q1 = std::string(QualityName(settings_.quality)) + " · " + ResolutionLabel(settings_) + Fmt(" · %d FPS", settings_.fps);
    std::string q2;
    if (!capsReady_) q2 = "Detectando aceleração de vídeo…";
    else {
        EncoderKind k = ResolveEncoder(settings_.CurrentCodec(), settings_.encoder, caps, nullptr);
        q2 = std::string(EncoderIsHardware(k) ? "Hardware: " : "") + EncoderDisplayName(k);
    }
    InfoCard("##c2", Icon::Gauge, kGreen, "QUALIDADE", q1.c_str(), q2.c_str(), ImVec2(cw, ch));

    auto devName = [](const std::string& id, const std::string& saved, const AudioDeviceList& list) -> std::string {
        if (id.empty()) return list.defaultName.empty() ? "Padrão do Windows" : list.defaultName;
        for (auto& d : list.devices)
            if (d.id == id) return d.name;
        return saved + " (indisponível)";
    };
    auto vol = [](int v) { return v == 100 ? std::string() : Fmt(" · %d%%", v); };
    std::string a1 = settings_.sysAudio ? "Sistema: " + devName(settings_.sysDeviceId, settings_.sysDeviceName, outDevs_) + vol(settings_.sysVolume)
                                        : "Som do sistema: desligado";
    std::string a2 = settings_.mic ? "Microfone: " + devName(settings_.micDeviceId, settings_.micDeviceName, inDevs_) + vol(settings_.micVolume)
                                   : "Microfone: desligado";
    InfoCard("##c3", Icon::Speaker, kAmber, "ÁUDIO", a1.c_str(), a2.c_str(), ImVec2(cw, ch));
    ImGui::SameLine();
    std::string d1 = diskTotal_ ? FormatBytes(diskFree_) + " livres de " + FormatBytes(diskTotal_) : "Espaço em disco indisponível";
    std::string d2 = "Pasta: " + Narrow(OutputDir());
    float used = diskTotal_ ? 1.0f - (float)((double)diskFree_ / (double)diskTotal_) : -1.0f;
    InfoCard("##c4", Icon::Disk, 0xA78BFA, Fmt("DISCO %s", Narrow(DriveLabel(OutputDir())).c_str()).c_str(), d1.c_str(), d2.c_str(),
             ImVec2(cw, ch), used);
    if (ImGui::IsItemHovered()) Tooltip(Narrow(OutputDir()).c_str());

    // ---- avisos
    DrawNotices();

    // ---- rodapé
    float footerY = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - S(34);
    if (ImGui::GetCursorScreenPos().y < footerY) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
        dl->AddLine(ImVec2(x, footerY - S(8)), ImVec2(x + W, footerY - S(8)), Col(kBorder));
        PushText(GetFonts().regular, 12.5f);
        std::string keys = "Atalhos: " + HotkeyText(settings_.hkStart) + " gravar  ·  " + HotkeyText(settings_.hkPause) +
                           " pausar  ·  " + HotkeyText(settings_.hkStop) + " finalizar";
        if (settings_.mic && settings_.hkMic.enabled) keys += "  ·  " + HotkeyText(settings_.hkMic) + " mic";
        dl->AddText(ImVec2(x, footerY), Col(kFaint), keys.c_str());
        std::string ver = Fmt("v%s · %s", APP_VERSION_U8, Paths().portable ? "portátil" : "instalado");
        float vw = ImGui::CalcTextSize(ver.c_str()).x;
        dl->AddText(ImVec2(x + W - vw, footerY), Col(kFaint), ver.c_str());
        PopText();
    }
}

void App::DrawRecordPanel() {
    float W = ImGui::GetContentRegionAvail().x;
    RecState st = recorder_->State();
    bool paused = st == RecState::Paused || st == RecState::Pausing;
    bool starting = st == RecState::Starting || st == RecState::Resuming;
    uint32_t accent = paused ? kAmber : (starting ? kBlue : kGreen);
    ImGui::PushStyleColor(ImGuiCol_Border, ColV(accent, 0.55f));
    ImGui::BeginChild("##recpanel", ImVec2(W, S(214)), ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float cw = ImGui::GetContentRegionAvail().x;
    // cronômetro grande
    std::string t = FormatDuration(recorder_->Elapsed());
    PushText(GetFonts().bold, 46);
    ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    dl->AddText(ImVec2(p.x + (cw - ts.x) * 0.5f, p.y), Col(paused ? kAmber : kText), t.c_str());
    PopText();
    // linha de estado
    const char* stText = starting ? "Iniciando a captura…" : paused ? "Pausado — nada está sendo gravado" : "Gravando";
    std::string what = currentTarget_.fullScreen ? currentTarget_.monitor.Title() : "Gravação livre (área selecionada)";
    std::string line = std::string(stText) + "  ·  " + what;
    PushText(GetFonts().semibold, 14);
    ImVec2 ls = ImGui::CalcTextSize(line.c_str());
    float ly = p.y + ts.y + S(2);
    float dotX = p.x + (cw - ls.x) * 0.5f - S(12);
    float a = paused ? 1.0f : 0.55f + 0.45f * (float)std::cos(ImGui::GetTime() * 5.2);
    dl->AddCircleFilled(ImVec2(dotX, ly + ls.y * 0.5f), S(5), Col(accent, a), 16);
    dl->AddText(ImVec2(p.x + (cw - ls.x) * 0.5f, ly), Col(accent), line.c_str());
    PopText();
    // detalhes técnicos
    RecorderStats s = recorder_->Stats();
    std::string size = (s.capW == s.outW && s.capH == s.outH) ? Fmt("%d×%d", s.outW, s.outH)
                                                              : Fmt("%d×%d (vídeo final %d×%d)", s.capW, s.capH, s.outW, s.outH);
    std::string det = Fmt("%s · %d FPS · %s · captura %s", size.c_str(), s.fps, s.encoder.c_str(), s.captureMethod.c_str());
    PushText(GetFonts().regular, 12.5f);
    ImVec2 dsz = ImGui::CalcTextSize(det.c_str());
    dl->AddText(ImVec2(p.x + std::max(0.0f, (cw - dsz.x) * 0.5f), ly + ls.y + S(6)), Col(kMuted), det.c_str());
    PopText();
    ImGui::SetCursorScreenPos(ImVec2(p.x, ly + ls.y + S(32)));
    // medidores
    if (recorder_->HasSys() || recorder_->HasMic()) {
        float mw = recorder_->HasSys() && recorder_->HasMic() ? (cw - S(16)) / 2 : cw;
        if (recorder_->HasSys()) {
            ImVec2 mp = ImGui::GetCursorScreenPos();
            DrawIcon(dl, Icon::Speaker, ImVec2(mp.x + S(9), mp.y + S(7)), S(15), Col(kMuted));
            ImGui::SetCursorScreenPos(ImVec2(mp.x + S(24), mp.y));
            LevelMeter("##ms", sysMeter_, ImVec2(mw - S(24), S(14)));
            if (recorder_->HasMic()) ImGui::SameLine(0, S(16));
        }
        if (recorder_->HasMic()) {
            ImVec2 mp = ImGui::GetCursorScreenPos();
            bool muted = recorder_->MicMuted();
            DrawIcon(dl, muted ? Icon::MicOff : Icon::Mic, ImVec2(mp.x + S(9), mp.y + S(7)), S(15), Col(muted ? kAmber : kMuted));
            ImGui::SetCursorScreenPos(ImVec2(mp.x + S(24), mp.y));
            if (muted) {
                // no lugar do medidor: aviso de que a voz não está indo para o vídeo
                ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(mw - S(24), S(14)));
                PushText(GetFonts().semibold, 12.5f);
                dl->AddText(ImVec2(a.x, a.y - S(1)), Col(kAmber), "Microfone silenciado");
                PopText();
            } else {
                LevelMeter("##mm", micMeter_, ImVec2(mw - S(24), S(14)));
            }
        }
    } else {
        HelpText("Gravando sem áudio.");
    }
    ImGui::Dummy(ImVec2(0, S(2)));
    bool withMic = recorder_->HasMic();
    float bw = withMic ? (cw - S(20)) / 3 : (cw - S(10)) / 2;
    bool canToggle = st == RecState::Recording || st == RecState::Paused;
    if (ActionButton("##pause", paused ? "Continuar" : "Pausar", paused ? Icon::Play : Icon::Pause, kBlueDark, ImVec2(bw, S(42)), canToggle))
        recorder_->TogglePause();
    if (withMic) {
        // silenciar/reativar só o microfone (o vídeo e o som do PC continuam)
        bool muted = recorder_->MicMuted();
        ImGui::SameLine(0, S(10));
        if (ActionButton("##mic", muted ? "Reativar mic" : "Silenciar mic", muted ? Icon::MicOff : Icon::Mic, muted ? 0xB45309 : 0x334155,
                         ImVec2(bw, S(42)), canToggle || starting))
            ToggleMicMute();
        if (ImGui::IsItemHovered())
            Tooltip(muted ? "Sua voz voltará a ser gravada." : "Para de gravar a sua voz sem parar o vídeo (para tossir, atender alguém...).");
    }
    ImGui::SameLine(0, S(10));
    if (ActionButton("##stop", "Finalizar", Icon::Stop, kRedDark, ImVec2(bw, S(42)), canToggle || starting)) recorder_->Stop();
    ImGui::EndChild();
    // descartar (discreto, com cara de link)
    PushText(GetFonts().regular, 12.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ColV(kPanelHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ColV(kBorder));
    ImGui::PushStyleColor(ImGuiCol_Text, ColV(kMuted));
    if (ImGui::SmallButton("Descartar esta gravação…")) {
        modal_ = Modal::ConfirmDiscard;
        openModalNow_ = true;
    }
    if (ImGui::IsItemHovered()) Tooltip("Apaga o que foi gravado nesta sessão (pede confirmação).");
    ImGui::PopStyleColor(4);
    PopText();
}

void App::DrawNotices() {
    if (notices_.empty()) return;
    float W = ImGui::GetContentRegionAvail().x;
    int remove = -1;
    for (size_t i = 0; i < notices_.size() && i < 4; i++) {
        Notice& n = notices_[i];
        uint32_t c = n.kind == Notice::Kind::Success ? kGreen : n.kind == Notice::Kind::Warning ? kAmber
                     : n.kind == Notice::Kind::Error ? kRed : kBlue;
        Icon ic = n.kind == Notice::Kind::Success ? Icon::Check : n.kind == Notice::Kind::Info ? Icon::Info : Icon::Warning;
        ImGui::PushID((int)i);
        PushText(GetFonts().regular, 13.5f);
        float textW = W - S(48) - S(36);
        float th = ImGui::CalcTextSize(n.text.c_str(), nullptr, false, textW).y;
        PopText();
        bool hasButtons = !n.file.empty() || n.actionId;
        float h = th + S(22) + (hasButtons ? S(34) : 0);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), Col(c, 0.08f), S(10));
        dl->AddRect(p, ImVec2(p.x + W, p.y + h), Col(c, 0.35f), S(10));
        DrawIcon(dl, ic, ImVec2(p.x + S(22), p.y + S(20)), S(17), Col(c));
        PushText(GetFonts().regular, 13.5f);
        dl->AddText(nullptr, 0, ImVec2(p.x + S(42), p.y + S(11)), Col(kText), n.text.c_str(), nullptr, textW);
        PopText();
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - S(32), p.y + S(6)));
        if (IconButton("##x", Icon::Close, kMuted, "Fechar aviso", S(26))) remove = (int)i;
        if (hasButtons) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + S(42), p.y + th + S(16)));
            if (!n.file.empty()) {
                if (GhostButton("##open", "Abrir vídeo", Icon::Play, ImVec2(S(120), S(28)))) OpenWithDefaultApp(n.file);
                ImGui::SameLine(0, S(8));
                if (GhostButton("##show", "Mostrar na pasta", Icon::Folder, ImVec2(S(150), S(28)))) RevealFileInExplorer(n.file);
            }
            if (n.actionId) {
                if (!n.file.empty()) ImGui::SameLine(0, S(8));
                if (GhostButton("##act", n.action.c_str(), Icon::Gear, ImVec2(S(170), S(28)))) {
                    // 1 = dispositivo de áudio, 2 = atalho, 3 = pasta de gravações
                    OpenSettings(n.actionId == 1 ? 1 : n.actionId == 3 ? 2 : 3);
                    remove = (int)i;
                }
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(8)));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (remove >= 0) notices_.erase(notices_.begin() + remove);
}

// ---------------------------------------------------------------------------
// Tela inteira: escolha do monitor
// ---------------------------------------------------------------------------
void App::DrawMonitors() {
    DrawHeader("Gravar tela inteira", true);
    HelpText("Escolha o monitor que será gravado. A gravação só começa quando você confirmar.");
    ImGui::Spacing();
    float W = ImGui::GetContentRegionAvail().x;
    int n = (int)monitors_.size();
    int cols = n <= 1 ? 1 : 2;
    float gap = S(12);
    float cw = (W - gap * (cols - 1)) / cols;
    float footer = S(64);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColV(0, 0));
    ImGui::BeginChild("##mons", ImVec2(W, ImGui::GetContentRegionAvail().y - footer), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    for (int i = 0; i < n; i++) {
        const MonitorInfo& m = monitors_[(size_t)i];
        if (i % cols != 0) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float thumbH = cw * 0.56f;
        float ch = thumbH + S(78);
        bool clicked = ImGui::InvisibleButton("##mon", ImVec2(cw, ch));
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (clicked) pickedMonitor_ = i;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            pickedMonitor_ = i;
            CaptureTarget t;
            t.fullScreen = true;
            t.monitor = m;
            t.region = m.rect;
            PostUi([this, t] { StartCapture(t, false); });   // fora do quadro atual
        }
        bool sel = pickedMonitor_ == i;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), Col(hov ? kPanelHi : kSurface), S(12));
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), Col(sel ? kBlue : kBorder), S(12), 0, sel ? S(2.5f) : 1.0f);
        // miniatura
        ImVec2 ta(p.x + S(10), p.y + S(10)), tb(p.x + cw - S(10), p.y + S(10) + thumbH - S(10));
        dl->AddRectFilled(ta, tb, Col(0x0B0E13), S(8));
        if (i < (int)thumbs_.size() && thumbs_[(size_t)i].srv) {
            const MonitorThumb& th = thumbs_[(size_t)i];
            float bw = tb.x - ta.x, bh = tb.y - ta.y;
            float sc = std::min(bw / th.w, bh / th.h);
            float iw = th.w * sc, ih = th.h * sc;
            ImVec2 ia(ta.x + (bw - iw) / 2, ta.y + (bh - ih) / 2);
            dl->AddImageRounded((ImTextureID)(intptr_t)th.srv, ia, ImVec2(ia.x + iw, ia.y + ih), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(6));
        } else {
            DrawIcon(dl, Icon::Monitor, ImVec2((ta.x + tb.x) / 2, (ta.y + tb.y) / 2), S(48), Col(kFaint));
        }
        if (sel) {
            ImVec2 cc(tb.x - S(18), ta.y + S(18));
            dl->AddCircleFilled(cc, S(13), Col(kBlue), 24);
            DrawIcon(dl, Icon::Check, cc, S(16), Col(0xFFFFFF));
        }
        PushText(GetFonts().semibold, 16);
        dl->AddText(ImVec2(p.x + S(14), p.y + thumbH + S(8)), Col(kText), m.Title().c_str());
        PopText();
        std::string det = Fmt("%d × %d pixels · escala %d%%", m.Width(), m.Height(), m.ScalePercent());
        if (!m.friendlyName.empty()) det += " · " + m.friendlyName;
        PushText(GetFonts().regular, 12.5f);
        dl->AddText(ImVec2(p.x + S(14), p.y + thumbH + S(32)), Col(kMuted), det.c_str());
        std::string how = m.dxgiAdapter >= 0 && !m.rotated ? "Captura pela placa de vídeo (DXGI)" : "Captura em modo compatibilidade (GDI)";
        dl->AddText(ImVec2(p.x + S(14), p.y + thumbH + S(50)), Col(kFaint), how.c_str());
        PopText();
        ImGui::PopID();
    }
    if (n == 0) HelpText("Nenhum monitor encontrado.");
    ImGui::EndChild();
    // rodapé
    ImGui::Spacing();
    float bw = (W - S(12)) / 2;
    if (GhostButton("##cancel", "Voltar", Icon::Back, ImVec2(bw, S(46)))) {
        releaseThumbs_ = true;   // a textura ainda é usada neste quadro: libera depois de desenhar
        page_ = Page::Home;
    }
    ImGui::SameLine(0, S(12));
    std::string label = n > 0 ? "Gravar " + monitors_[(size_t)std::clamp(pickedMonitor_, 0, n - 1)].Title() : "Gravar";
    if (ActionButton("##go", label.c_str(), Icon::Record, kGreenDark, ImVec2(bw, S(46)), n > 0)) {
        CaptureTarget t;
        t.fullScreen = true;
        t.monitor = monitors_[(size_t)std::clamp(pickedMonitor_, 0, n - 1)];
        t.region = t.monitor.rect;
        releaseThumbs_ = true;
        page_ = Page::Home;
        PostUi([this, t] { StartCapture(t, false); });
    }
}

// ---------------------------------------------------------------------------
// Configurações
// ---------------------------------------------------------------------------
void App::OpenSettings(int tab) {
    edit_ = settings_;
    settingsTab_ = tab;
    page_ = Page::Settings;
    RefreshAudioDevices();
    RequestFrames();
}

void App::DrawSettings() {
    DrawHeader("Configurações", true);
    float W = ImGui::GetContentRegionAvail().x;
    Segmented("##tabs", kTabNames, 5, &settingsTab_, W);
    if (recorder_->IsBusy()) {
        ImGui::Spacing();
        HelpText("Há uma gravação em andamento: as mudanças valem a partir da próxima gravação.");
    }
    ImGui::Spacing();
    float footer = S(62);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(14)));
    ImGui::BeginChild("##settings_body", ImVec2(W, ImGui::GetContentRegionAvail().y - footer), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PushItemWidth(-1);
    switch (settingsTab_) {
        case 0: DrawSettingsVideo(); break;
        case 1: DrawSettingsAudio(); break;
        case 2: DrawSettingsFiles(); break;
        case 3: DrawSettingsHotkeys(); break;
        case 4: DrawSettingsAdvanced(); break;
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    if (settingsTab_ != 1) StopMonitoring();
    // rodapé
    ImGui::Spacing();
    float bw = S(150);
    if (GhostButton("##defaults", "Restaurar padrões", Icon::Refresh, ImVec2(S(176), S(42)))) {
        std::string keepDir = edit_.outputDir;
        edit_ = Settings();
        edit_.outputDir = keepDir;    // a pasta escolhida continua (é o mais chato de configurar de novo)
        Toast("Valores padrão aplicados — clique em Salvar para confirmar.");
    }
    ImGui::SameLine(W - bw * 2 - S(10) + ImGui::GetStyle().WindowPadding.x);
    if (GhostButton("##cancelset", "Cancelar", Icon::Close, ImVec2(bw, S(42)))) {
        capturingHotkey_ = 0;
        StopMonitoring();
        page_ = Page::Home;
        ApplyHotkeys(false);
    }
    ImGui::SameLine(0, S(10));
    if (ActionButton("##save", "Salvar", Icon::Check, kGreenDark, ImVec2(bw, S(42)))) SaveSettingsFromEditor();
}

static void Label(const char* text) {
    PushText(GetFonts().semibold, 14.5f);
    ImGui::TextUnformatted(text);
    PopText();
}

void App::DrawSettingsVideo() {
    Label("Formato do arquivo");
    const char* fmts[] = {"MP4", "AVI"};
    int c = (int)edit_.container;
    if (Segmented("##fmt", fmts, 2, &c)) edit_.container = (Container)c;
    HelpText(edit_.container == Container::MP4
                 ? "MP4: o formato mais compatível — abre no Windows, celulares, navegadores e editores de vídeo."
                 : "AVI: para programas e aparelhos antigos. Os codecs abaixo são os compatíveis com AVI de verdade "
                   "(o arquivo não é só um MP4 com outra extensão).");
    ImGui::Spacing();

    Label("Codec de vídeo");
    std::vector<VideoCodec> codecs = edit_.container == Container::MP4 ? std::vector<VideoCodec>{VideoCodec::H264, VideoCodec::HEVC}
                                                                       : std::vector<VideoCodec>{VideoCodec::MPEG4, VideoCodec::MJPEG, VideoCodec::H264};
    std::vector<std::string> names;
    int cur = 0;
    VideoCodec& target = edit_.container == Container::MP4 ? edit_.codecMp4 : edit_.codecAvi;
    for (size_t i = 0; i < codecs.size(); i++) {
        names.push_back(CodecLongName(codecs[i]));
        if (codecs[i] == target) cur = (int)i;
    }
    if (ComboBox("##codec", &cur, names)) target = codecs[(size_t)cur];
    if (target == VideoCodec::HEVC)
        HelpText("H.265 gera arquivos ~40% menores, mas o player padrão do Windows pode pedir a \"Extensão de Vídeo HEVC\". "
                 "VLC e navegadores modernos tocam normalmente.");
    else if (edit_.container == Container::AVI && target == VideoCodec::H264)
        HelpText("H.264 dentro de AVI funciona no VLC e no player do Windows 10/11, mas alguns programas antigos não aceitam.");
    else if (target == VideoCodec::MJPEG)
        HelpText("Motion JPEG: compatibilidade máxima e edição fácil, porém arquivos bem maiores.");
    ImGui::Spacing();

    Label("Codificador (aceleração por hardware)");
    EncoderCaps caps = GetEncoderCaps();
    bool hwCodec = target == VideoCodec::H264 || target == VideoCodec::HEVC;
    bool hevc = target == VideoCodec::HEVC;
    std::vector<std::string> enc = {"Automático (usa a placa de vídeo se houver)", "Software (processador)",
                                    std::string("NVIDIA NVENC") + ((hevc ? caps.nvencHevc : caps.nvencH264) ? "" : " — não detectado"),
                                    std::string("Intel Quick Sync") + ((hevc ? caps.qsvHevc : caps.qsvH264) ? "" : " — não detectado"),
                                    std::string("AMD AMF") + ((hevc ? caps.amfHevc : caps.amfH264) ? "" : " — não detectado")};
    std::vector<bool> enEnc = {true, true, hevc ? caps.nvencHevc : caps.nvencH264, hevc ? caps.qsvHevc : caps.qsvH264,
                               hevc ? caps.amfHevc : caps.amfH264};
    int e = (int)edit_.encoder;
    ImGui::BeginDisabled(!hwCodec);
    if (ComboBox("##enc", &e, enc, -1, &enEnc)) edit_.encoder = (EncoderPref)e;
    ImGui::EndDisabled();
    if (!capsReady_) HelpText("Detectando as placas de vídeo…");
    else {
        EncoderKind k = ResolveEncoder(target, edit_.encoder, caps, nullptr);
        HelpText(hwCodec ? ("Será usado: " + std::string(EncoderDisplayName(k)) +
                            (EncoderIsHardware(k) ? " — leve para o processador." : " — usa mais o processador."))
                               .c_str()
                         : "Este codec é sempre codificado pelo processador.");
    }
    ImGui::Spacing();

    Label("Qualidade");
    const char* qs[] = {"Recomendada", "Arquivo menor", "Superior", "Taxa fixa"};
    int q = (int)edit_.quality;
    if (Segmented("##q", qs, 4, &q)) edit_.quality = (QualityPreset)q;
    if (edit_.quality == QualityPreset::CustomBitrate) {
        ImGui::SliderInt("##br", &edit_.customBitrateKbps, 1000, 60000, "%d kbps");
        HelpText("Referência: 1080p a 30 FPS, cerca de 6.000 a 10.000 kbps · 4K a 30 FPS, cerca de 25.000 a 40.000 kbps. Valores maiores = melhor qualidade e arquivos maiores.");
    } else {
        HelpText(edit_.quality == QualityPreset::Recommended ? "Equilíbrio entre nitidez e tamanho — ideal para o dia a dia."
                 : edit_.quality == QualityPreset::Small   ? "Arquivos bem menores (bom para enviar por WhatsApp/e-mail); textos pequenos perdem um pouco de nitidez."
                                                           : "Máxima nitidez (textos e detalhes finos); arquivos maiores.");
    }
    ImGui::Spacing();

    Label("Resolução de saída");
    std::vector<std::string> res;
    for (int i = 0; i <= 6; i++) res.push_back(ResolutionName((ResolutionPreset)i));
    int r = (int)edit_.resolution;
    if (ComboBox("##res", &r, res)) edit_.resolution = (ResolutionPreset)r;
    if (edit_.resolution == ResolutionPreset::Custom2060) {
        float hw = (ImGui::GetContentRegionAvail().x - S(30)) / 2;
        ImGui::SetNextItemWidth(hw);
        ImGui::InputInt("##cw", &edit_.customW, 2, 16);
        ImGui::SameLine();
        ImGui::TextUnformatted("×");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(hw);
        ImGui::InputInt("##chh", &edit_.customH, 2, 16);
        HelpText("\"2060p\" não é um padrão fixo de mercado, então aqui você define largura e altura. Use números pares "
                 "(ex.: 3662 × 2060 para 16:9). Limite: 7680 × 4320.");
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(edit_.resolution == ResolutionPreset::Original);
    Label("Proporção");
    const char* asp[] = {"Barras pretas", "Sem barras", "Esticar"};
    int a = (int)edit_.aspect;
    if (Segmented("##asp", asp, 3, &a)) edit_.aspect = (AspectMode)a;
    HelpText(edit_.aspect == AspectMode::Letterbox ? "Mantém a proporção e completa com barras pretas — o vídeo sai exatamente no tamanho escolhido (recomendado)."
             : edit_.aspect == AspectMode::Fit     ? "Mantém a proporção sem barras — o tamanho final se ajusta à área gravada."
                                                   : "Estica a imagem até o tamanho escolhido (pode deformar).");
    ImGui::EndDisabled();
    if (edit_.resolution != ResolutionPreset::Original)
        HelpText("Se a área gravada for menor que a resolução escolhida, a imagem é ampliada — isso aumenta o tamanho, mas não cria detalhes que não existem.");
    ImGui::Spacing();

    Label("Quadros por segundo (FPS)");
    const char* fps[] = {"15", "24", "30", "60"};
    int fi = edit_.fps == 15 ? 0 : edit_.fps == 24 ? 1 : edit_.fps == 60 ? 3 : 2;
    if (Segmented("##fps", fps, 4, &fi)) edit_.fps = fi == 0 ? 15 : fi == 1 ? 24 : fi == 3 ? 60 : 30;
    HelpText("30 FPS serve para quase tudo. 60 FPS deixa jogos e rolagens mais fluidos, mas pesa mais.");
    ImGui::Spacing();
    ToggleRow("##mouse", &edit_.drawMouse, "Mostrar o cursor do mouse no vídeo");
}

// Linha de estado embaixo do medidor: avisa em vermelho se o dispositivo não abriu
static void AudioStatusLine(const AudioCapture* cap) {
    if (!cap) return;
    AudioCapture::Status st = cap->GetStatus();
    if (st != AudioCapture::Status::Failed && st != AudioCapture::Status::Reconnecting) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ColV(st == AudioCapture::Status::Failed ? kRed : kAmber));
    ImGui::PushTextWrapPos(0);
    std::string t = st == AudioCapture::Status::Failed ? "Não foi possível abrir: " + cap->LastError() + "."
                                                        : "Dispositivo desconectado — tentando reconectar…";
    ImGui::TextUnformatted(t.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void App::DrawSettingsAudio() {
    StartMonitoring();
    auto deviceCombo = [&](const char* id, std::string& devId, std::string& devName, const AudioDeviceList& list) {
        std::vector<std::string> items;
        items.push_back("Padrão do Windows" + (list.defaultName.empty() ? std::string() : " (" + list.defaultName + ")"));
        int cur = 0;
        bool found = devId.empty();
        for (size_t i = 0; i < list.devices.size(); i++) {
            items.push_back(list.devices[i].name);
            if (list.devices[i].id == devId) { cur = (int)i + 1; found = true; }
        }
        if (!found) {
            items.push_back(devName + " (desconectado)");
            cur = (int)items.size() - 1;
        }
        if (ComboBox(id, &cur, items)) {
            if (cur == 0) { devId.clear(); devName.clear(); }
            else if (cur <= (int)list.devices.size()) {
                devId = list.devices[(size_t)cur - 1].id;
                devName = list.devices[(size_t)cur - 1].name;
            }
        }
    };
    float W = ImGui::GetContentRegionAvail().x;
    // ---- sistema
    ImGui::BeginChild("##sysbox", ImVec2(W, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Speaker, ImVec2(p.x + S(11), p.y + S(11)), S(19), Col(kAmber));
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(30), p.y));
        Label("Áudio do sistema");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - S(30));
        Toggle("##sysOn", &edit_.sysAudio);
        HelpText("Tudo que toca no computador: vídeos, navegador, chamadas, jogos, avisos do Windows.");
        if (edit_.sysAudio) {
            deviceCombo("##sysdev", edit_.sysDeviceId, edit_.sysDeviceName, outDevs_);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Volume");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            ImGui::SliderInt("##sysvol", &edit_.sysVolume, 0, 300, "%d%%");
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
            LevelMeter("##sysm", sysMeter_, ImVec2(ImGui::GetContentRegionAvail().x, S(18)));
            AudioStatusLine(monSys_.get());
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    // ---- microfone
    ImGui::BeginChild("##micbox", ImVec2(W, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Mic, ImVec2(p.x + S(11), p.y + S(11)), S(19), Col(kBlue));
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(30), p.y));
        Label("Microfone (narração)");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - S(30));
        Toggle("##micOn", &edit_.mic);
        HelpText(("Opcional: grava sua voz junto. Durante a gravação, o botão \"Mic\" da barra (ou " + HotkeyText(edit_.hkMic) +
                  ") silencia e reativa só o microfone, sem parar o vídeo.").c_str());
        if (edit_.mic) {
            deviceCombo("##micdev", edit_.micDeviceId, edit_.micDeviceName, inDevs_);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Volume");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            ImGui::SliderInt("##micvol", &edit_.micVolume, 0, 300, "%d%%");
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
            LevelMeter("##micm", micMeter_, ImVec2(ImGui::GetContentRegionAvail().x, S(18)));
            AudioStatusLine(monMic_.get());
        }
    }
    ImGui::EndChild();
    if (edit_.sysAudio && edit_.mic) {
        ImGui::Spacing();
        // Mistura: quem fica mais alto no vídeo, o som do PC ou a sua voz
        Label("Mistura: som do PC x sua voz");
        struct MixPreset { const char* name; int sys, mic; };
        static const MixPreset presets[] = {{"Equilibrado", 100, 100}, {"Comentar vídeo", 30, 150}, {"Destacar o PC", 100, 60}};
        const char* names[] = {presets[0].name, presets[1].name, presets[2].name};
        int cur = -1;
        for (int i = 0; i < 3; i++)
            if (edit_.sysVolume == presets[i].sys && edit_.micVolume == presets[i].mic) cur = i;
        if (Segmented("##mix", names, 3, &cur) && cur >= 0) {
            edit_.sysVolume = presets[cur].sys;
            edit_.micVolume = presets[cur].mic;
        }
        std::string mixHelp =
            cur == 0   ? "Som do PC e voz no mesmo volume."
            : cur == 1 ? "Para narrar por cima de um vídeo: o som do PC fica baixo (30%) e a sua voz bem alta (150%)."
            : cur == 2 ? "O som do PC em primeiro plano e a sua voz mais baixa (60%)."
                       : Fmt("Personalizado: som do PC %d%% · sua voz %d%%.", edit_.sysVolume, edit_.micVolume);
        mixHelp += " Ajuste fino nos controles de Volume acima (0 a 300%); um limitador evita que o som estoure.";
        HelpText(mixHelp.c_str());
        ImGui::Spacing();
    }
    if (GhostButton("##refreshdev", "Atualizar lista de dispositivos", Icon::Refresh, ImVec2(S(260), S(32)))) {
        RefreshAudioDevices();
        StopMonitoring();
    }
    HelpText("Os medidores mostram o nível ao vivo: toque algum som ou fale no microfone para testar.");
    ImGui::Spacing();
    if (edit_.sysAudio && edit_.mic) {
        Label("Faixas de áudio");
        const char* tr[] = {"Misturar numa faixa", "Faixas separadas"};
        int t = (int)edit_.tracks;
        if (Segmented("##tracks", tr, 2, &t)) edit_.tracks = (AudioTracks)t;
        HelpText(edit_.tracks == AudioTracks::Mixed
                     ? "Sistema e microfone viram uma faixa só, com o volume de cada um ajustado acima (toca em qualquer player)."
                     : "Duas faixas no mesmo arquivo (1: sistema, 2: microfone) — ótimo para editar depois. Alguns players tocam só a primeira.");
        ImGui::Spacing();
    }
    Label("Qualidade do áudio");
    if (edit_.container == Container::MP4) {
        std::vector<std::string> br = {"96 kbps", "128 kbps", "160 kbps (recomendado)", "192 kbps", "256 kbps", "320 kbps"};
        int vals[] = {96, 128, 160, 192, 256, 320};
        int cur = 2;
        for (int i = 0; i < 6; i++) if (vals[i] == edit_.aacBitrate) cur = i;
        if (ComboBox("##aac", &cur, br)) edit_.aacBitrate = vals[cur];
        HelpText("MP4 usa áudio AAC (padrão em todo player).");
    } else {
        const char* ac[] = {"MP3", "PCM (sem compressão)"};
        int a = (int)edit_.aviAudio;
        if (Segmented("##aviaudio", ac, 2, &a)) edit_.aviAudio = (AviAudio)a;
        if (edit_.aviAudio == AviAudio::MP3) {
            std::vector<std::string> br = {"128 kbps", "160 kbps", "192 kbps (recomendado)", "256 kbps", "320 kbps"};
            int vals[] = {128, 160, 192, 256, 320};
            int cur = 2;
            for (int i = 0; i < 5; i++) if (vals[i] == edit_.mp3Bitrate) cur = i;
            if (ComboBox("##mp3", &cur, br)) edit_.mp3Bitrate = vals[cur];
        } else {
            HelpText("PCM é áudio sem perdas (cerca de 11 MB por minuto).");
        }
    }
}

void App::DrawSettingsFiles() {
    Label("Pasta de destino dos vídeos");
    std::string dir = edit_.outputDir.empty() ? Narrow(Paths().defaultRecordingsDir) + "  (padrão)" : edit_.outputDir;
    ImGui::BeginDisabled(true);
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", dir.c_str());
    ImGui::InputText("##dir", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    if (GhostButton("##pick", "Alterar…", Icon::Folder, ImVec2(S(120), S(32)))) {
        std::wstring chosen;
        std::wstring initial = edit_.outputDir.empty() ? Paths().defaultRecordingsDir : Widen(edit_.outputDir);
        if (PickFolderDialog(hwnd_, initial, chosen)) {
            if (!IsDirWritable(chosen)) ShowError("Pasta sem permissão", "Não é possível gravar arquivos em:\n" + Narrow(chosen) + "\n\nEscolha outra pasta.");
            else edit_.outputDir = Narrow(chosen);
        }
    }
    ImGui::SameLine();
    if (GhostButton("##openo", "Abrir", Icon::Open, ImVec2(S(100), S(32)))) {
        std::wstring d = edit_.outputDir.empty() ? Paths().defaultRecordingsDir : Widen(edit_.outputDir);
        EnsureDir(d);
        OpenFolderInExplorer(d);
    }
    ImGui::SameLine();
    if (GhostButton("##defdir", "Usar a padrão", Icon::Refresh, ImVec2(S(150), S(32)))) edit_.outputDir.clear();
    {
        uint64_t fr = 0, tt = 0;
        std::wstring d = edit_.outputDir.empty() ? Paths().defaultRecordingsDir : Widen(edit_.outputDir);
        if (GetDiskSpace(d, fr, tt)) HelpText(("Espaço livre nesse disco: " + FormatBytes(fr) + " de " + FormatBytes(tt)).c_str());
        if (Paths().portable && edit_.outputDir.empty())
            HelpText("No modo portátil a pasta padrão é a \"recordings\" ao lado do programa (vai junto no pendrive).");
    }
    ImGui::Spacing();

    Label("Nome dos arquivos");
    char pat[256];
    snprintf(pat, sizeof(pat), "%s", edit_.namePattern.c_str());
    if (ImGui::InputText("##pattern", pat, sizeof(pat))) edit_.namePattern = pat;
    std::string ext = Narrow(ContainerExtension(edit_.container));
    HelpText(("Exemplo: " + ExpandNamePattern(edit_.namePattern, "TelaInteira", 1) + ext).c_str());
    HelpText("Códigos: {data} 2026-10-11 · {hora} 15-30-00 · {ano} {mes} {dia} · {modo} TelaInteira/Area · {monitor} Monitor1. "
             "Se já existir um arquivo com o mesmo nome, o programa acrescenta (2), (3)… — nunca sobrescreve.");
    ImGui::Spacing();

    Label("Depois de finalizar uma gravação");
    std::vector<std::string> af;
    for (int i = 0; i <= 4; i++) af.push_back(AfterFinishName((AfterFinish)i));
    int a = (int)edit_.afterFinish;
    if (ComboBox("##after", &a, af)) edit_.afterFinish = (AfterFinish)a;
    HelpText("Uma notificação perto do relógio sempre avisa quando o vídeo fica pronto.");
    ImGui::Spacing();

    Label("Contagem regressiva antes de gravar");
    const char* cd[] = {"Não", "3 s", "5 s", "10 s"};
    int ci = edit_.countdown == 0 ? 0 : edit_.countdown == 5 ? 2 : edit_.countdown == 10 ? 3 : 1;
    if (Segmented("##cd", cd, 4, &ci)) edit_.countdown = ci == 0 ? 0 : ci == 2 ? 5 : ci == 3 ? 10 : 3;
    ImGui::Spacing();
    ToggleRow("##min", &edit_.minimizeOnRecord, "Minimizar esta janela ao começar a gravar");
    ToggleRow("##border", &edit_.showRegionBorder, "Mostrar moldura verde em volta da área (gravação livre)");
    HelpText("A moldura fica do lado de fora da área e não aparece no vídeo.");
}

bool App::HotkeyCaptureWidget(const char* id, Hotkey* hk, int slot) {
    ImGui::PushID(id);
    bool changed = false;
    bool capturing = capturingHotkey_ == slot;
    if (capturing && hotkeyCaptured_) {
        *hk = capturedHotkey_;
        capturingHotkey_ = 0;
        hotkeyCaptured_ = false;
        capturing = false;
        changed = true;
        ApplyHotkeys(false);   // devolve os atalhos atuais enquanto não salvar
    }
    std::string label = capturing ? "Pressione a combinação… (Esc cancela)" : HotkeyText(*hk);
    if (GhostButton("##hk", label.c_str(), Icon::Keyboard, ImVec2(S(280), S(34)))) {
        capturingHotkey_ = slot;
        hotkeyCaptured_ = false;
        hotkeys_.UnregisterAll();   // senão a própria tecla seria "engolida" pelo atalho global
    }
    ImGui::SameLine(0, S(12));
    bool en = hk->enabled;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
    if (ToggleRow("##en", &en, hk->enabled ? "Ativo" : "Desligado")) {
        hk->enabled = en;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

void App::DrawSettingsHotkeys() {
    HelpText("Os atalhos funcionam mesmo com outro programa em primeiro plano. Para não atrapalhar outros programas, "
             "o atalho de iniciar só fica ativo enquanto o " APP_NAME_U8 " está esperando, e os de pausar, finalizar e microfone só durante a gravação.");
    ImGui::Spacing();
    Label("Iniciar gravação");
    HotkeyCaptureWidget("start", &edit_.hkStart, 1);
    std::vector<std::string> acts = {"Abrir o menu Gravar (escolher o modo)", "Gravar tela inteira do monitor onde está o mouse",
                                     "Gravação livre (selecionar área)"};
    int a = (int)edit_.startAction;
    if (ComboBox("##startact", &a, acts)) edit_.startAction = (StartAction)a;
    if (!hotkeys_.LastFailure(HK_START).empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ColV(kRed));
        ImGui::TextWrapped("Não registrado: %s", hotkeys_.LastFailure(HK_START).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    Label("Pausar / continuar");
    HotkeyCaptureWidget("pause", &edit_.hkPause, 2);
    if (!hotkeys_.LastFailure(HK_PAUSE).empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ColV(kRed));
        ImGui::TextWrapped("Não registrado: %s", hotkeys_.LastFailure(HK_PAUSE).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    Label("Finalizar gravação");
    HotkeyCaptureWidget("stop", &edit_.hkStop, 3);
    if (!hotkeys_.LastFailure(HK_STOP).empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ColV(kRed));
        ImGui::TextWrapped("Não registrado: %s", hotkeys_.LastFailure(HK_STOP).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    Label("Silenciar / reativar o microfone");
    HotkeyCaptureWidget("mic", &edit_.hkMic, 4);
    if (!hotkeys_.LastFailure(HK_MIC).empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ColV(kRed));
        ImGui::TextWrapped("Não registrado: %s", hotkeys_.LastFailure(HK_MIC).c_str());
        ImGui::PopStyleColor();
    }
    HelpText("Só durante a gravação e com o microfone ligado (Configurações > Áudio). O vídeo continua gravando.");
    ImGui::Spacing();
    Label("Cancelar a seleção de área");
    HelpText("Esc (fixo) — também funciona na contagem regressiva. O botão direito do mouse também cancela.");
    if (edit_.hkStart.enabled && edit_.hkPause.enabled && edit_.hkStart == edit_.hkPause)
        HelpText("Atenção: iniciar e pausar estão com a mesma tecla.");
}

void App::DrawSettingsAdvanced() {
    Label("Método de captura");
    std::vector<std::string> cm = {"Automático (recomendado)", "DXGI — pela placa de vídeo (mais leve)", "GDI — compatibilidade (mais pesado)"};
    int c = (int)edit_.captureMethod;
    if (ComboBox("##capm", &c, cm)) edit_.captureMethod = (CaptureMethod)c;
    HelpText("O automático usa a Desktop Duplication (DXGI) e, se ela falhar (área de trabalho remota, monitor girado, "
             "alguns notebooks com duas placas), troca sozinho para GDI.");
    ImGui::Spacing();

    Label("Modo portátil");
    bool portable = Paths().portable;
    if (ToggleRow("##portable", &portable, Paths().portable ? "Ligado (configurações junto ao programa)" : "Desligado (configurações no %APPDATA%)")) {
        std::string err;
        if (!SetPortableMode(portable, &err)) ShowError("Modo portátil", err);
        else Toast(portable ? "Modo portátil ligado." : "Modo portátil desligado.");
    }
    HelpText(("Configurações e logs ficam em: " + Narrow(Paths().dataDir)).c_str());
    ImGui::Spacing();

    Label("Teste de desempenho");
    HelpText("Grava a tela por 5 segundos (sem salvar) com as configurações desta tela e mede se o computador aguenta.");
    if (ActionButton("##perf", perfRunning_ ? "Testando…" : "Testar por 5 segundos", Icon::Gauge, kBlueDark, ImVec2(S(240), S(38)),
                     !perfRunning_ && capsReady_ && !recorder_->IsBusy()))
        RunPerformanceTest();
    {
        std::lock_guard<std::mutex> lk(perfMtx_);
        if (!perfResult_.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(perfResult_.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();

    Label("Diagnóstico");
    EncoderCaps caps = GetEncoderCaps();
    std::string diag;
    diag += "Sistema: " + WindowsVersionText() + "\n";
    diag += std::string("Excluir barra da captura: ") + (IsCaptureExclusionSupported() ? "suportado" : "NÃO suportado (Windows antigo)") + "\n";
    std::string ffv = caps.ffmpegVersion;
    if (StartsWith(ffv, "ffmpeg version ")) ffv = ffv.substr(15);
    if (ffv.find(" Copyright") != std::string::npos) ffv = ffv.substr(0, ffv.find(" Copyright"));
    diag += "ffmpeg: " + (caps.ffmpegOk ? ffv : std::string("não encontrado")) + "\n";
    diag += "Hardware: ";
    std::string hw;
    if (caps.nvencH264) hw += "NVENC H.264" + std::string(caps.nvencHevc ? "/H.265 " : " ");
    if (caps.qsvH264) hw += "Quick Sync H.264" + std::string(caps.qsvHevc ? "/H.265 " : " ");
    if (caps.amfH264) hw += "AMF H.264" + std::string(caps.amfHevc ? "/H.265 " : " ");
    diag += (capsReady_ ? (hw.empty() ? "nenhum (usa o processador)" : hw) : "detectando…") + "\n";
    for (auto& m : monitors_)
        diag += Fmt("%s: %dx%d, escala %d%%, %s\n", m.Title().c_str(), m.Width(), m.Height(), m.ScalePercent(),
                    m.dxgiAdapter >= 0 ? ("GPU " + m.adapterName).c_str() : "sem DXGI");
    PushText(GetFonts().regular, 12.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ColV(kMuted));
    ImGui::TextUnformatted(diag.c_str());
    ImGui::PopStyleColor();
    PopText();
    if (GhostButton("##logs", "Abrir pasta de logs", Icon::Folder, ImVec2(S(200), S(32)))) OpenFolderInExplorer(Paths().logsDir);
    ImGui::SameLine();
    if (GhostButton("##logf", "Abrir log de hoje", Icon::Open, ImVec2(S(180), S(32)))) OpenWithDefaultApp(Log::CurrentFile());
    ImGui::Spacing();
    Label("Sobre");
    HelpText(APP_NAME_U8 " " APP_VERSION_U8 " — gravador de tela portátil para Windows 10/11 (64 bits).\n"
             "Criado por " APP_AUTHOR_U8 ". Usa FFmpeg (GPL) e Dear ImGui (MIT); licenças na pasta \"licencas\".\n"
             "Nada é enviado para a internet: vídeos, configurações e logs ficam só neste computador.");
}

void App::SaveSettingsFromEditor() {
    edit_.Sanitize();
    if (!edit_.outputDir.empty()) {
        std::wstring d = Widen(edit_.outputDir);
        if (!EnsureDir(d) || !IsDirWritable(d)) {
            ShowError("Pasta de destino", "Não é possível gravar na pasta escolhida:\n" + edit_.outputDir);
            return;
        }
    }
    auto same = [](const Hotkey& a, const Hotkey& b) { return a.enabled && b.enabled && a.vk == b.vk && a.mods == b.mods; };
    const Hotkey* hk[] = {&edit_.hkStart, &edit_.hkPause, &edit_.hkStop, &edit_.hkMic};
    bool dup = false;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) dup = dup || same(*hk[i], *hk[j]);
    if (dup) {
        ShowError("Atalhos repetidos", "Dois atalhos estão com a mesma tecla. Escolha teclas diferentes.");
        return;
    }
    bool dirChanged = edit_.outputDir != settings_.outputDir;
    settings_ = edit_;
    std::string err;
    if (!SaveSettings(settings_, &err)) {
        ShowError("Configurações", "As configurações valem até fechar o programa, mas não puderam ser salvas:\n" + err);
    } else {
        Toast("Configurações salvas.");
    }
    capturingHotkey_ = 0;
    ApplyHotkeys(true);
    if (dirChanged) {
        library_.Refresh(OutputDir());
        RefreshDisk();
        WarnIfFat32();
    }
    StopMonitoring();
    page_ = Page::Home;
}

void App::RunPerformanceTest() {
    if (perfRunning_) return;
    if (perfThread_.joinable()) perfThread_.join();
    perfRunning_ = true;
    {
        std::lock_guard<std::mutex> lk(perfMtx_);
        perfResult_ = "Gravando 5 segundos do monitor principal…";
    }
    Settings s = edit_;
    RefreshMonitors(false);
    MonitorInfo mon;
    for (auto& m : monitors_)
        if (m.primary) mon = m;
    if (mon.deviceName.empty() && !monitors_.empty()) mon = monitors_[0];
    perfThread_ = std::thread([this, s, mon] {
        ScopedCom com;
        SegmentPlan p;
        p.fps = s.fps;
        p.drawMouse = s.drawMouse;
        p.capW = mon.Width() & ~1;
        p.capH = mon.Height() & ~1;
        p.useDDA = s.captureMethod != CaptureMethod::GDI && mon.dxgiAdapter >= 0 && !mon.rotated;
        p.ddaAdapter = mon.dxgiAdapter;
        p.ddaOutput = mon.dxgiOutput;
        p.fullMonitor = true;
        p.capX = p.useDDA ? 0 : mon.rect.left;
        p.capY = p.useDDA ? 0 : mon.rect.top;
        ComputeOutputSize(s, p.capW, p.capH, p.outW, p.outH);
        p.aspect = s.aspect;
        p.codec = s.CurrentCodec();
        p.encoder = ResolveEncoder(p.codec, s.encoder, GetEncoderCaps(), nullptr);
        p.quality = s.quality;
        p.bitrateKbps = s.customBitrateKbps;
        p.forAvi = s.container == Container::AVI;
        p.testSeconds = 5;
        std::string result;
        for (int attempt = 0; attempt < 2; attempt++) {
            p.baseUs = WallClockUs() - 1000000;
            ChildProcess proc;
            std::mutex m;
            FfProgress prog;
            std::vector<std::string> tail;
            proc.OnStdout([&](const std::string& l) { std::lock_guard<std::mutex> lk(m); ParseProgressLine(l, prog); });
            proc.OnStderr([&](const std::string& l) { std::lock_guard<std::mutex> lk(m); tail.push_back(l); Log::Raw(l); });
            std::string err;
            int64_t t0 = QpcNowUs();
            if (!proc.Start(Paths().ffmpegExe, BuildSegmentArgs(p), L"", &err)) { result = "Falhou: " + err; break; }
            proc.CloseStdin();
            if (!proc.Wait(30000)) proc.Kill();
            double wall = (QpcNowUs() - t0) / 1e6;
            double cpu = proc.CpuSeconds();
            proc.JoinReaders();
            if (proc.ExitCode() != 0 || prog.frame == 0) {
                bool dda = false, enc = false, d1, d2;
                std::string why = ExplainFfmpegError(tail, &dda, &enc, &d1, &d2);
                if (attempt == 0 && p.useDDA) { p.useDDA = false; p.capX = mon.rect.left; p.capY = mon.rect.top; continue; }
                if (attempt == 0 && EncoderIsHardware(p.encoder)) { FallbackEncoder(p.encoder, p.encoder); continue; }
                result = "O teste falhou: " + why;
                break;
            }
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            double cpuPct = wall > 0 ? cpu / wall / std::max<DWORD>(1, si.dwNumberOfProcessors) * 100.0 : 0;
            double fpsAvg = prog.frame / 5.0;
            bool ok = prog.speed >= 0.95 && fpsAvg >= s.fps * 0.93;
            auto dec = [](double v, int casas) { std::string t = Fmt("%.*f", casas, v); for (auto& ch : t) if (ch == '.') ch = ','; return t; };
            result = Fmt("Resultado: %s FPS de %d · velocidade %sx · %lld quadros repetidos · CPU usada pelo ffmpeg: %.0f%%\n"
                         "%s · %s · %dx%d\n%s",
                         dec(fpsAvg, 1).c_str(), s.fps, dec(prog.speed, 2).c_str(), (long long)prog.dupFrames, cpuPct,
                         p.useDDA ? "Captura DXGI" : "Captura GDI", EncoderDisplayName(p.encoder), p.outW, p.outH,
                         ok ? "APROVADO: seu computador dá conta desta configuração."
                            : "ATENÇÃO: o computador não acompanhou. Reduza a resolução/FPS ou use um codificador de hardware.");
            Log::Info("Teste de desempenho: %s", result.c_str());
            break;
        }
        {
            std::lock_guard<std::mutex> lk(perfMtx_);
            perfResult_ = result;
        }
        perfRunning_ = false;
        PostUi([] {});
    });
}

// ---------------------------------------------------------------------------
// Biblioteca de gravações
// ---------------------------------------------------------------------------
void App::DrawLibrary() {
    DrawHeader("Gravações", true);
    float W = ImGui::GetContentRegionAvail().x;
    std::wstring dir = OutputDir();
    PushText(GetFonts().regular, 12.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ColV(kMuted));
    ImGui::TextUnformatted(("Pasta: " + Narrow(dir)).c_str());
    ImGui::PopStyleColor();
    PopText();
    ImGui::SetNextItemWidth(W - S(330));
    ImGui::InputTextWithHint("##search", "Buscar pelo nome…", librarySearch_, sizeof(librarySearch_));
    ImGui::SameLine();
    if (GhostButton("##openlib", "Abrir pasta", Icon::Folder, ImVec2(S(150), S(34)))) {
        EnsureDir(dir);
        OpenFolderInExplorer(dir);
    }
    ImGui::SameLine();
    if (GhostButton("##reflib", "Atualizar", Icon::Refresh, ImVec2(S(150), S(34)))) library_.Refresh(dir);

    auto items = library_.Items();
    std::string filter = ToLower(librarySearch_);
    if (items.empty()) {
        ImGui::Dummy(ImVec2(0, S(40)));
        float cx = ImGui::GetCursorScreenPos().x + W / 2;
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Film, ImVec2(cx, ImGui::GetCursorScreenPos().y + S(24)), S(46), Col(kFaint));
        ImGui::Dummy(ImVec2(0, S(56)));
        const char* t = "Nenhuma gravação nesta pasta ainda.";
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(t).x) / 2);
        ImGui::TextDisabled("%s", t);
        return;
    }
    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                         ImGuiTableFlags_PadOuterX;
    auto niceCodec = [](std::string c) {
        c = ToLower(c);
        if (c == "h264") return std::string("H.264");
        if (c == "hevc") return std::string("H.265");
        if (c == "mpeg4") return std::string("MPEG-4");
        if (c == "mjpeg") return std::string("MJPEG");
        return c;
    };
    if (ImGui::BeginTable("##lib", 3, tf, ImVec2(W, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Gravação (nome e data)", ImGuiTableColumnFlags_WidthStretch, 2.3f);
        ImGui::TableSetupColumn("Duração · tamanho · formato", ImGuiTableColumnFlags_WidthStretch, 1.9f);
        ImGui::TableSetupColumn("Ações", ImGuiTableColumnFlags_WidthFixed, S(4 * 30 + 10));
        ImGui::TableHeadersRow();
        int idx = 0;
        for (auto& it : items) {
            if (!filter.empty() && ToLower(it.name).find(filter) == std::string::npos) continue;
            ImGui::PushID(idx++);
            ImGui::TableNextRow(0, S(50));
            ImGui::TableSetColumnIndex(0);
            PushText(GetFonts().semibold, 13.5f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(it.name.c_str());
            ImGui::PopTextWrapPos();
            PopText();
            if (ImGui::IsItemHovered()) Tooltip(Narrow(it.path).c_str());
            PushText(GetFonts().regular, 12.0f);
            ImGui::TextDisabled("%s", FormatDateTime(it.modified).c_str());
            PopText();
            ImGui::TableSetColumnIndex(1);
            std::string l1 = (it.infoReady ? (it.info.ok ? FormatDuration(it.info.duration, false) : std::string("—")) : std::string("…")) +
                             "  ·  " + FormatBytes(it.size);
            ImGui::TextUnformatted(l1.c_str());
            std::string l2;
            if (!it.infoReady) l2 = "lendo informações…";
            else if (!it.info.ok) l2 = "arquivo ilegível ou incompleto";
            else {
                l2 = it.info.container + " · " + niceCodec(it.info.vcodec);
                if (it.info.width) l2 += Fmt(" · %d×%d", it.info.width, it.info.height);
            }
            PushText(GetFonts().regular, 12.0f);
            ImGui::TextDisabled("%s", l2.c_str());
            PopText();
            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
            if (IconButton("##play", Icon::Play, kGreen, "Abrir o vídeo")) OpenWithDefaultApp(it.path);
            ImGui::SameLine(0, S(2));
            if (IconButton("##dir", Icon::Folder, kBlue, "Mostrar na pasta")) RevealFileInExplorer(it.path);
            ImGui::SameLine(0, S(2));
            if (IconButton("##copy", Icon::Copy, kText, "Copiar o caminho do arquivo")) {
                if (CopyTextToClipboard(hwnd_, it.path)) Toast("Caminho copiado.");
            }
            ImGui::SameLine(0, S(2));
            if (IconButton("##del", Icon::Trash, kRed, "Excluir (pede confirmação)", 0, true)) {
                deleteTarget_ = it.path;
                modal_ = Modal::ConfirmDelete;
                openModalNow_ = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (library_.Busy()) {
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        Spinner(ImVec2(wp.x + ws.x - S(30), wp.y + S(30)), S(8), Col(kBlue));
    }
}

// ---------------------------------------------------------------------------
// Diálogos
// ---------------------------------------------------------------------------
static bool BeginModal(const char* name, float width) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(width, vp->WorkSize.x - S(30)), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    bool open = ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                          ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    return open;
}

static void ModalTitle(Icon icon, uint32_t color, const char* title) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(p.x + S(16), p.y + S(16)), S(16), Col(color, 0.16f), 24);
    DrawIcon(dl, icon, ImVec2(p.x + S(16), p.y + S(16)), S(18), Col(color));
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(44), p.y + S(4)));
    PushText(GetFonts().bold, 18);
    ImGui::TextUnformatted(title);
    PopText();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + S(42)));
}

void App::DrawModals() {
    const char* names[] = {"", "##m_error", "##m_delete", "##m_discard", "##m_exit", "##m_recovery", "##m_folder", "##m_about"};
    if (openModalNow_ && modal_ != Modal::None) {
        ImGui::OpenPopup(names[(int)modal_]);
        openModalNow_ = false;
    }
    float bw = S(140), bh = S(40);
    auto rightButtons = [&](int count) {
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - count * bw - (count - 1) * S(10));
    };
    // ---- erro
    if (BeginModal("##m_error", S(470))) {
        ModalTitle(Icon::Warning, kRed, modalTitle_.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(420));
        ImGui::TextUnformatted(modalText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Spacing();
        rightButtons(2);
        if (GhostButton("##viewlog", "Ver o log", Icon::Open, ImVec2(bw, bh))) OpenWithDefaultApp(Log::CurrentFile());
        ImGui::SameLine(0, S(10));
        if (ActionButton("##ok", "OK", Icon::Check, kBlueDark, ImVec2(bw, bh))) {
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // ---- excluir
    if (BeginModal("##m_delete", S(470))) {
        ModalTitle(Icon::Trash, kRed, "Excluir gravação?");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(420));
        PushText(GetFonts().semibold, 14);
        ImGui::TextUnformatted(Narrow(FileNameOf(deleteTarget_)).c_str());
        PopText();
        HelpText("O arquivo vai para a Lixeira (em pendrives e HDs externos o Windows exclui direto).");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        rightButtons(2);
        if (GhostButton("##no", "Cancelar", Icon::Close, ImVec2(bw, bh))) {
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(10));
        if (ActionButton("##yes", "Excluir", Icon::Trash, kRedDark, ImVec2(bw, bh))) {
            std::string err;
            if (MoveToRecycleBin(deleteTarget_, &err)) {
                library_.Remove(deleteTarget_);
                Log::Info("Gravação excluída: %s", Narrow(deleteTarget_).c_str());
                Toast("Gravação excluída.");
                RefreshDisk();
                modal_ = Modal::None;
            } else {
                modalTitle_ = "Não foi possível excluir";
                modalText_ = err;
                modal_ = Modal::Error;
                openModalNow_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // ---- descartar
    if (BeginModal("##m_discard", S(470))) {
        ModalTitle(Icon::Warning, kAmber, "Descartar a gravação?");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(420));
        ImGui::TextUnformatted("Tudo o que foi gravado nesta sessão será apagado e nenhum vídeo será salvo.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        rightButtons(2);
        if (GhostButton("##keep", "Continuar", Icon::Back, ImVec2(bw, bh))) {
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(10));
        if (ActionButton("##discard", "Descartar", Icon::Trash, kRedDark, ImVec2(bw, bh))) {
            recorder_->Discard();
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // ---- sair durante a gravação
    if (BeginModal("##m_exit", S(490))) {
        ModalTitle(Icon::Warning, kAmber, "Uma gravação está em andamento");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(440));
        ImGui::TextUnformatted(finalizing_ ? "O vídeo ainda está sendo processado. O programa fecha assim que terminar."
                                           : "Para sair, a gravação será finalizada e salva normalmente.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        rightButtons(2);
        if (GhostButton("##back2", "Voltar", Icon::Back, ImVec2(bw, bh))) {
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(10));
        if (ActionButton("##finexit", "Salvar e sair", Icon::Stop, kRedDark, ImVec2(bw, bh))) {
            quitAfterFinalize_ = true;
            recorder_->Stop();
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // ---- recuperação
    if (BeginModal("##m_recovery", S(500))) {
        ModalTitle(Icon::Refresh, kBlue, "Gravação interrompida encontrada");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(450));
        Session s;
        std::string when;
        if (!pendingSessions_.empty() && Session::Load(pendingSessions_.front(), s, nullptr)) when = s.created;
        ImGui::TextUnformatted(Fmt("Uma gravação de %s não foi finalizada (o programa ou o computador fechou de repente). "
                                   "Os trechos gravados foram preservados.",
                                   when.empty() ? "uma sessão anterior" : when.c_str())
                                   .c_str());
        if (pendingSessions_.size() > 1) HelpText(Fmt("Há mais %zu gravações pendentes.", pendingSessions_.size() - 1).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        rightButtons(3);
        if (GhostButton("##later", "Agora não", Icon::Clock, ImVec2(bw, bh))) {
            pendingSessions_.clear();
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(10));
        if (GhostButton("##delrec", "Excluir", Icon::Trash, ImVec2(bw, bh))) {
            if (!pendingSessions_.empty()) {
                DeleteDirectoryTree(pendingSessions_.front());
                pendingSessions_.erase(pendingSessions_.begin());
            }
            if (pendingSessions_.empty()) { modal_ = Modal::None; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine(0, S(10));
        if (ActionButton("##recover", "Recuperar", Icon::Check, kGreenDark, ImVec2(bw, bh))) {
            if (!pendingSessions_.empty()) {
                StartRecovery(pendingSessions_.front());
                pendingSessions_.erase(pendingSessions_.begin());
            }
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // ---- pasta indisponível
    if (BeginModal("##m_folder", S(500))) {
        ModalTitle(Icon::Folder, kAmber, "Pasta de gravações indisponível");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(450));
        ImGui::TextUnformatted(("A pasta configurada não foi encontrada (pendrive/HD externo desconectado ou letra de unidade diferente):\n\n" +
                                settings_.outputDir)
                                   .c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        rightButtons(2);
        if (GhostButton("##usedef", "Usar a padrão", Icon::Refresh, ImVec2(bw, bh))) {
            settings_.outputDir.clear();
            SaveSettings(settings_, nullptr);
            EnsureDir(OutputDir());
            library_.Refresh(OutputDir());
            RefreshDisk();
            modal_ = Modal::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(10));
        if (ActionButton("##choose", "Escolher…", Icon::Folder, kBlueDark, ImVec2(bw, bh))) {
            std::wstring chosen;
            if (PickFolderDialog(hwnd_, Paths().defaultRecordingsDir, chosen) && IsDirWritable(chosen)) {
                settings_.outputDir = Narrow(chosen);
                SaveSettings(settings_, nullptr);
                library_.Refresh(OutputDir());
                RefreshDisk();
                WarnIfFat32();
                modal_ = Modal::None;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void App::DrawToast() {
    if (toast_.empty()) return;
    if (ImGui::GetTime() > toastUntil_) {
        toast_.clear();
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    PushText(GetFonts().semibold, 13.5f);
    ImVec2 ts = ImGui::CalcTextSize(toast_.c_str());
    float w = ts.x + S(36), h = S(38);
    ImVec2 p(vp->WorkPos.x + (vp->WorkSize.x - w) / 2, vp->WorkPos.y + vp->WorkSize.y - h - S(48));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(0x2A3242, 0.97f), h / 2);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Col(kBorder), h / 2);
    dl->AddText(ImVec2(p.x + S(18), p.y + (h - ts.y) / 2), Col(kText), toast_.c_str());
    PopText();
}
