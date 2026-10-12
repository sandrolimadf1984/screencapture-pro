#include "app.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>

#include "finalize.h"
#include "log.h"
#include "paths.h"
#include "resource.h"
#include "ui.h"
#include "win_util.h"
#include "../third_party/imgui/backends/imgui_impl_dx11.h"
#include "../third_party/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* kMainClass = APP_MAIN_CLASS_W;

enum TrayCmd { TC_FULL = 1001, TC_REGION, TC_PAUSE, TC_STOP, TC_SHOW, TC_FOLDER, TC_LIBRARY, TC_EXIT, TC_MIC };

// ===========================================================================
// Janela + Direct3D 11
// ===========================================================================
bool App::CreateDeviceD3D() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd,
                                               &swap_, &dev_, &got, &ctx_);
    if (FAILED(hr)) {
        // sem placa de vídeo compatível (máquina virtual, área de trabalho remota...): renderizador por software
        Log::Warn("D3D11 por hardware falhou (%s); usando WARP.", HResultText(hr).c_str());
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd, &swap_,
                                           &dev_, &got, &ctx_);
    }
    if (FAILED(hr)) {
        Log::Error("Direct3D 11 indisponível: %s", HResultText(hr).c_str());
        return false;
    }
    // não deixa o Alt+Enter do DXGI mexer na janela
    IDXGIFactory* fac = nullptr;
    if (SUCCEEDED(swap_->GetParent(__uuidof(IDXGIFactory), (void**)&fac)) && fac) {
        fac->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        fac->Release();
    }
    CreateRenderTarget();
    return true;
}

void App::CleanupDeviceD3D() {
    CleanupRenderTarget();
    SafeRelease(swap_);
    SafeRelease(ctx_);
    SafeRelease(dev_);
}

void App::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swap_->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        dev_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
}

void App::CleanupRenderTarget() { SafeRelease(rtv_); }

bool App::CreateMainWindow(int show) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = inst_;
    wc.hIcon = iconApp_;
    wc.hIconSm = iconAppSmall_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    HMONITOR prim = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dpi = DpiForMonitor(prim);
    dpiScale_ = dpi / 96.0f;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(prim, &mi);
    int w = (int)(600 * dpiScale_), h = (int)(780 * dpiScale_);
    int areaW = mi.rcWork.right - mi.rcWork.left, areaH = mi.rcWork.bottom - mi.rcWork.top;
    w = std::min(w, areaW - 40);
    h = std::min(h, areaH - 40);
    int x = mi.rcWork.left + (areaW - w) / 2, y = mi.rcWork.top + (areaH - h) / 2;
    hwnd_ = CreateWindowExW(0, kMainClass, APP_NAME_W, WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst_, this);
    if (!hwnd_) return false;
    SetDarkTitleBar(hwnd_);
    UINT realDpi = DpiForWindow(hwnd_);
    if (realDpi != dpi) dpiScale_ = realDpi / 96.0f;
    if (!CreateDeviceD3D()) {
        FatalMessage(APP_NAME_U8, "Não foi possível iniciar o Direct3D 11 (necessário para a interface).\n\n"
                                  "Atualize o driver da placa de vídeo. Detalhes no log.");
        return false;
    }
    ShowWindow(hwnd_, show == SW_SHOWMINIMIZED || show == SW_MINIMIZE ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK App::StaticWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        app = reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)app);
        app->hwnd_ = h;
    }
    if (app) return app->WndProc(h, m, wp, lp);
    return DefWindowProcW(h, m, wp, lp);
}

LRESULT App::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    // captura de tecla para configurar atalho (antes do ImGui ver a tecla)
    if (capturingHotkey_ && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN)) {
        UINT vk = (UINT)wp;
        if (vk == VK_ESCAPE) { capturingHotkey_ = 0; RequestFrames(); return 0; }
        if (vk != VK_CONTROL && vk != VK_SHIFT && vk != VK_MENU && vk != VK_LWIN && vk != VK_RWIN && vk != VK_LCONTROL &&
            vk != VK_RCONTROL && vk != VK_LSHIFT && vk != VK_RSHIFT && vk != VK_LMENU && vk != VK_RMENU) {
            capturedHotkey_ = Hotkey{true, 0, vk};
            if (GetKeyState(VK_CONTROL) & 0x8000) capturedHotkey_.mods |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) capturedHotkey_.mods |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) & 0x8000) capturedHotkey_.mods |= MOD_SHIFT;
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) capturedHotkey_.mods |= MOD_WIN;
            hotkeyCaptured_ = true;
            RequestFrames();
        }
        return 0;
    }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, m, wp, lp)) return TRUE;
    if (m == taskbarCreatedMsg_ && taskbarCreatedMsg_) {
        // o Explorer reiniciou: o ícone da bandeja precisa ser recriado
        tray_.Destroy();
        tray_.Create(hwnd_, WM_APP_TRAY, recordingUi_ ? iconRec_ : iconAppSmall_, APP_NAME_W);
        return 0;
    }
    switch (m) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                resizeW_ = LOWORD(lp);
                resizeH_ = HIWORD(lp);
                RequestFrames();
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = (LONG)(500 * dpiScale_);
            mmi->ptMinTrackSize.y = (LONG)(620 * dpiScale_);
            return 0;
        }
        case WM_DPICHANGED: {
            OnDpiChanged(HIWORD(wp));
            RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;   // Alt sozinho não abre menu
            break;
        case WM_CLOSE:
            if ((recorder_ && recorder_->IsBusy()) || finalizing_) {
                ShowWindowNormal();
                modal_ = Modal::ConfirmExit;
                openModalNow_ = true;
                RequestFrames();
                return 0;
            }
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_HOTKEY: {
            int id = (int)wp;
            Log::Info("Atalho global acionado: %d", id);
            if (id == HK_START && !recorder_->IsBusy() && !selector_.Active() && !countdown_.Active()) {
                if (settings_.startAction == StartAction::Region) BeginRegionSelection();
                else if (settings_.startAction == StartAction::FullScreen) {
                    RefreshMonitors(false);
                    POINT pt;
                    GetCursorPos(&pt);
                    int mi = MonitorIndexForPoint(monitors_, pt);
                    if (mi >= 0) {
                        CaptureTarget t;
                        t.fullScreen = true;
                        t.monitor = monitors_[(size_t)mi];
                        t.region = t.monitor.rect;
                        StartCapture(t, true);
                    }
                } else {
                    OpenRecordChooser();
                }
            } else if (id == HK_PAUSE) {
                recorder_->TogglePause();
            } else if (id == HK_STOP) {
                recorder_->Stop();
            } else if (id == HK_MIC) {
                ToggleMicMute();
            }
            RequestFrames();
            return 0;
        }
        case WM_APP_TRAY: {
            UINT ev = LOWORD(lp);
            if (ev == WM_CONTEXTMENU || ev == WM_RBUTTONUP) ShowTrayMenu();
            else if (ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == WM_LBUTTONDBLCLK) ShowWindowNormal();
            else if (ev == NIN_BALLOONUSERCLICK) {
                if (!lastFile_.empty() && FileExists(lastFile_)) RevealFileInExplorer(lastFile_);
                else ShowWindowNormal();
            }
            return 0;
        }
        case WM_APP_WAKE:
            return 0;
        case WM_DISPLAYCHANGE:
            RefreshMonitors(page_ == Page::Monitors);
            RequestFrames();
            break;
        case WM_POWERBROADCAST:
            // ao suspender o PC, finaliza a gravação para não perder nada
            if (wp == PBT_APMSUSPEND && recorder_ && recorder_->IsBusy()) {
                Log::Warn("Windows entrando em suspensão: finalizando a gravação.");
                recorder_->Stop();
            }
            break;
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void App::OnDpiChanged(UINT dpi) {
    dpiScale_ = dpi / 96.0f;
    pendingDpi_ = true;
    RequestFrames();
}

// ===========================================================================
// Ciclo principal
// ===========================================================================
int App::Run(HINSTANCE inst, int show) {
    inst_ = inst;
    iconApp_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    iconAppSmall_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    iconRec_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_REC), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!iconRec_) iconRec_ = iconAppSmall_;

    std::string warn;
    LoadSettings(settings_, &warn);
    recorder_ = std::make_unique<Recorder>([this](const RecorderEvent& e) { PostUi([this, e] { OnRecorderEvent(e); }); });
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!CreateMainWindow(show)) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;    // nada de imgui.ini espalhado por aí (modo portátil)
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    UI::LoadFonts();
    UI::ApplyStyle(dpiScale_);
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(dev_, ctx_);

    if (!warn.empty()) AddNotice(Notice::Kind::Warning, warn);
    Startup();

    while (running_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.hwnd == hwnd_ || msg.hwnd == nullptr) RequestFrames(3);
        }
        if (!running_) break;
        DrainUi();
        Tick();
        bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
        if (visible && (framesPending_ > 0 || Animating())) {
            RenderFrame();
            if (framesPending_ > 0) framesPending_--;
            if (framesPending_ == 0 && Animating()) {
                // animação leve: ~30 quadros/s com medidores, ~10 com só o cronômetro
                bool meters = (page_ == Page::Settings && settingsTab_ == 1) || perfRunning_;
                MsgWaitForMultipleObjectsEx(0, nullptr, meters ? 30 : 90, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        } else {
            MsgWaitForMultipleObjectsEx(0, nullptr, recordingUi_ ? 100 : 400, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    Shutdown();
    return 0;
}

bool App::Animating() const {
    if (finalizing_ || perfRunning_ || !capsReady_ || library_.Busy()) return true;
    if (recordingUi_ || (recorder_ && recorder_->IsBusy())) return true;
    if (page_ == Page::Settings && settingsTab_ == 1) return true;
    if (!toast_.empty()) return true;
    if (capturingHotkey_) return true;
    return false;
}

void App::RenderFrame() {
    if (pendingDpi_) {
        UI::ApplyStyle(dpiScale_);
        pendingDpi_ = false;
    }
    if (resizeW_ != 0 && resizeH_ != 0) {
        CleanupRenderTarget();
        swap_->ResizeBuffers(0, resizeW_, resizeH_, DXGI_FORMAT_UNKNOWN, 0);
        resizeW_ = resizeH_ = 0;
        CreateRenderTarget();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawRoot();
    ImGui::Render();
    const float clear[4] = {0x11 / 255.0f, 0x15 / 255.0f, 0x1C / 255.0f, 1.0f};
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = swap_->Present(1, 0);
    if (releaseThumbs_) {
        releaseThumbs_ = false;
        ReleaseThumbs();
    }
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        // driver de vídeo reiniciou: recria tudo
        Log::Warn("Dispositivo Direct3D perdido; recriando.");
        ReleaseThumbs();
        ImGui_ImplDX11_Shutdown();
        CleanupDeviceD3D();
        if (CreateDeviceD3D()) ImGui_ImplDX11_Init(dev_, ctx_);
    }
}

void App::PostUi(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(uiMtx_);
        uiQueue_.push_back(std::move(fn));
    }
    if (hwnd_) PostMessageW(hwnd_, WM_APP_WAKE, 0, 0);
}

void App::DrainUi() {
    std::vector<std::function<void()>> q;
    {
        std::lock_guard<std::mutex> lk(uiMtx_);
        q.swap(uiQueue_);
    }
    for (auto& f : q) f();
    if (!q.empty()) RequestFrames(3);
}

void App::Tick() {
    double now = (double)GetTickCount64() / 1000.0;
    if (now - lastTick_ >= 0.1) {
        lastTick_ = now;
        float sp = recorder_->TakeSysPeak(), mp = recorder_->TakeMicPeak();
        sysMeter_ = std::max(sp * settings_.sysVolume / 100.0f, sysMeter_ * 0.75f);
        micMeter_ = std::max(mp * settings_.micVolume / 100.0f, micMeter_ * 0.75f);
        if (monSys_) sysMeter_ = std::max(monSys_->TakePeak() * edit_.sysVolume / 100.0f, sysMeter_ * 0.75f);
        if (monMic_) micMeter_ = std::max(monMic_->TakePeak() * edit_.micVolume / 100.0f, micMeter_ * 0.75f);
        if (recordingUi_ && bar_.Visible()) {
            RecState st = recorder_->State();
            int barState = st == RecState::Recording ? 1 : (st == RecState::Paused || st == RecState::Pausing) ? 2
                           : (st == RecState::Stopping || st == RecState::Finalizing) ? 3 : 0;
            bar_.Update(barState, recorder_->Elapsed(), recorder_->HasSys(), sysMeter_, recorder_->HasMic(), micMeter_,
                        recorder_->MicMuted());
        }
    }
    if (now - lastTrayTip_ >= 1.0) {
        lastTrayTip_ = now;
        std::string tip = APP_NAME_U8;
        RecState st = recorder_->State();
        if (st == RecState::Recording) tip += " — gravando " + FormatDuration(recorder_->Elapsed());
        else if (st == RecState::Paused) tip += " — pausado " + FormatDuration(recorder_->Elapsed());
        else if (finalizing_) tip += Fmt(" — processando %d%%", (int)(finalizeProgress_ * 100));
        else tip += " — pronto";
        if ((st == RecState::Recording || st == RecState::Paused) && recorder_->MicMuted()) tip += " · microfone mudo";
        tray_.SetTip(Widen(tip));
    }
    if (page_ == Page::Home && now - lastDiskCheck_ > 5.0) RefreshDisk();
    // codificadores detectados enquanto o usuário esperava para gravar
    if (pendingStart_ && capsReady_) {
        pendingStart_ = false;
        DoStartCapture(pendingTarget_);
    }
}

// ===========================================================================
// Início / fim
// ===========================================================================
void App::Startup() {
    Log::Info("%s %s iniciado | %s | DPI %.0f%% | pasta: %s | modo %s", APP_NAME_U8, APP_VERSION_U8, WindowsVersionText().c_str(),
              dpiScale_ * 100, Narrow(Paths().exeDir).c_str(), Paths().portable ? "portátil" : "instalado");
    RefreshMonitors(false);
    for (auto& m : monitors_)
        Log::Info("Monitor %d: %s %dx%d em (%ld,%ld) escala %d%% %s | DXGI adaptador %d saída %d (%s)", m.number,
                  Narrow(m.deviceName).c_str(), m.Width(), m.Height(), m.rect.left, m.rect.top, m.ScalePercent(),
                  m.primary ? "principal" : "", m.dxgiAdapter, m.dxgiOutput, m.adapterName.c_str());
    RefreshAudioDevices();
    RefreshDisk();

    // detecção de ffmpeg + codificadores de hardware em segundo plano (alguns segundos)
    probeThread_ = std::thread([this] {
        ScopedCom com;
        ProbeEncodersBlocking();
        capsReady_ = true;
        PostUi([this] {
            EncoderCaps c = GetEncoderCaps();
            if (!c.ffmpegOk) {
                // erro clássico: abrir o .exe com dois cliques de DENTRO do .zip (o
                // Windows extrai só o .exe para uma pasta temporária, sem o ffmpeg)
                std::wstring dir = Paths().exeDir;
                for (auto& ch : dir) ch = (wchar_t)towlower(ch);
                bool fromZip = dir.find(L".zip\\") != std::wstring::npos || dir.find(L"\\rar$ex") != std::wstring::npos ||
                               dir.find(L"\\7zo") != std::wstring::npos;
                if (fromZip)
                    ShowError("Extraia o programa antes de usar",
                              "O " APP_NAME_U8 " foi aberto direto de dentro do arquivo .zip, e assim o Windows não "
                              "leva junto o componente de vídeo (ffmpeg).\n\n"
                              "Feche o programa, clique com o botão direito no arquivo .zip, escolha \"Extrair tudo...\" "
                              "e depois abra o " APP_EXE_U8 " que está dentro da pasta extraída.");
                else
                    ShowError("Componente de vídeo ausente",
                              "O arquivo ffmpeg\\ffmpeg.exe não foi encontrado ou não pôde ser executado.\n\n"
                              "Ele precisa estar na pasta \"ffmpeg\" ao lado do " APP_EXE_U8 ". "
                              "Extraia o pacote completo novamente (não copie só o .exe).");
            }
        });
    });

    tray_.Create(hwnd_, WM_APP_TRAY, iconAppSmall_, APP_NAME_W);
    hotkeys_.Attach(hwnd_);
    ApplyHotkeys(true);
    library_.Refresh(OutputDir());

    WinVersion v = GetWindowsVersion();
    if (v.major < 10)
        AddNotice(Notice::Kind::Warning, "Este programa foi feito para Windows 10 e 11 (64 bits). Neste Windows alguns recursos podem não funcionar.");
    else if (!IsCaptureExclusionSupported())
        AddNotice(Notice::Kind::Info, "Seu Windows 10 é anterior à versão 2004: a barra flutuante será posicionada fora da área gravada "
                                      "(ou escondida) para não aparecer no vídeo. Atualize o Windows para usar a exclusão automática.");
    if (FileExists(PortableMarkerPath()) && !Paths().portable)
        AddNotice(Notice::Kind::Warning, "A pasta do programa não permite gravação (pendrive protegido?). As configurações estão sendo "
                                         "salvas em %APPDATA%\\" APP_ID_U8 ".");

    CheckOutputFolder();
    WarnIfFat32();
    CheckAudioDevices();
    if (modal_ == Modal::None) {
        pendingSessions_ = FindPendingSessions(OutputDir());
        if (!pendingSessions_.empty()) {
            modal_ = Modal::Recovery;
            openModalNow_ = true;
        }
    }
}

void App::Shutdown() {
    Log::Info("Encerrando o programa.");
    StopMonitoring();
    selector_.Cancel();
    countdown_.Cancel();
    bar_.Hide();
    border_.Hide();
    recorder_.reset();       // se ainda houver gravação, finaliza antes de sair
    library_.Shutdown();
    if (probeThread_.joinable()) probeThread_.join();
    if (perfThread_.joinable()) perfThread_.join();
    hotkeys_.UnregisterAll();
    tray_.Destroy();
    ReleaseThumbs();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
}

// ===========================================================================
// Dados auxiliares
// ===========================================================================
std::wstring App::OutputDir() const {
    return settings_.outputDir.empty() ? Paths().defaultRecordingsDir : Widen(settings_.outputDir);
}

void App::RefreshMonitors(bool thumbs) {
    monitors_ = EnumerateMonitors();
    if (!thumbs) return;
    ReleaseThumbs();
    for (auto& m : monitors_) {
        MonitorThumb t;
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (CaptureScreenScaled(m.rect, 480, 300, px, w, h) && dev_) {
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = (UINT)w;
            d.Height = (UINT)h;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init = {px.data(), (UINT)w * 4, 0};
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(dev_->CreateTexture2D(&d, &init, &tex)) && tex) {
                dev_->CreateShaderResourceView(tex, nullptr, &t.srv);
                tex->Release();
                t.w = w;
                t.h = h;
            }
        }
        thumbs_.push_back(t);
    }
}

void App::ReleaseThumbs() {
    for (auto& t : thumbs_) SafeRelease(t.srv);
    thumbs_.clear();
}

void App::RefreshAudioDevices() {
    outDevs_ = ListAudioDevices(true);
    inDevs_ = ListAudioDevices(false);
}

void App::RefreshDisk() {
    lastDiskCheck_ = (double)GetTickCount64() / 1000.0;
    if (!GetDiskSpace(OutputDir(), diskFree_, diskTotal_)) diskFree_ = diskTotal_ = 0;
}

void App::CheckOutputFolder() {
    std::wstring dir = OutputDir();
    if (settings_.outputDir.empty()) {
        EnsureDir(dir);
        return;
    }
    if (!DirExists(dir) && !EnsureDir(dir)) {
        Log::Warn("Pasta de gravações indisponível: %s", Narrow(dir).c_str());
        modal_ = Modal::FolderMissing;
        openModalNow_ = true;
    }
}

void App::WarnIfFat32() {
    // Pendrives costumam vir em FAT32, que não aceita arquivos acima de 4 GB
    // (mais ou menos 1 hora de vídeo em 1080p). Melhor avisar antes.
    std::wstring fs = FileSystemName(OutputDir());
    if (_wcsicmp(fs.c_str(), L"FAT32") == 0 || _wcsicmp(fs.c_str(), L"FAT") == 0) {
        Log::Warn("Pasta de gravações em disco %s.", Narrow(fs).c_str());
        AddNotice(Notice::Kind::Warning,
                  "A pasta de gravações está num disco formatado em " + Narrow(fs) +
                      ", que não aceita arquivos maiores que 4 GB (cerca de 1 hora em 1080p). Para gravações longas, "
                      "escolha uma pasta no disco do computador ou use um pendrive/HD em exFAT ou NTFS.",
                  3, "Escolher pasta");
    }
}

void App::CheckAudioDevices() {
    auto check = [&](bool enabled, const std::string& id, const std::string& name, const AudioDeviceList& list, const char* what) {
        if (!enabled || id.empty()) return;
        for (auto& d : list.devices)
            if (d.id == id) return;
        AddNotice(Notice::Kind::Warning,
                  Fmt("O dispositivo %s \"%s\" não está disponível neste computador. Será usado o dispositivo padrão do Windows.", what,
                      name.empty() ? "salvo" : name.c_str()),
                  1, "Escolher outro");
    };
    check(settings_.sysAudio, settings_.sysDeviceId, settings_.sysDeviceName, outDevs_, "de áudio do sistema");
    check(settings_.mic, settings_.micDeviceId, settings_.micDeviceName, inDevs_, "de microfone");
    if (settings_.sysAudio && outDevs_.ok && outDevs_.devices.empty())
        AddNotice(Notice::Kind::Warning, "Nenhum dispositivo de saída de áudio foi encontrado. As gravações ficarão sem o som do sistema "
                                         "até um alto-falante/fone ser conectado.");
}

void App::StartMonitoring() {
    // medidores ao vivo na aba Áudio das configurações
    std::string sysId = edit_.sysAudio ? (edit_.sysDeviceId.empty() ? "<padrao>" : edit_.sysDeviceId) : "";
    std::string micId = edit_.mic ? (edit_.micDeviceId.empty() ? "<padrao>" : edit_.micDeviceId) : "";
    if (sysId != monSysId_) {
        monSys_.reset();
        monSysId_ = sysId;
        if (!sysId.empty()) {
            monSys_ = std::make_unique<AudioCapture>(AudioKind::SystemLoopback, edit_.sysDeviceId);
            monSys_->Start();
        }
    }
    if (micId != monMicId_) {
        monMic_.reset();
        monMicId_ = micId;
        if (!micId.empty()) {
            monMic_ = std::make_unique<AudioCapture>(AudioKind::Microphone, edit_.micDeviceId);
            monMic_->Start();
        }
    }
}

void App::StopMonitoring() {
    monSys_.reset();
    monMic_.reset();
    monSysId_.clear();
    monMicId_.clear();
}

// ===========================================================================
// Avisos
// ===========================================================================
void App::AddNotice(Notice::Kind k, const std::string& text, int actionId, const std::string& action) {
    for (auto& n : notices_)
        if (n.text == text) return;   // não repete o mesmo aviso
    Notice n;
    n.kind = k;
    n.text = text;
    n.actionId = actionId;
    n.action = action;
    n.time = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
    notices_.insert(notices_.begin(), n);
    if (notices_.size() > 6) notices_.pop_back();
    RequestFrames();
}

void App::ShowError(const std::string& title, const std::string& text) {
    Log::Error("%s: %s", title.c_str(), text.c_str());
    modal_ = Modal::Error;
    modalTitle_ = title;
    modalText_ = text;
    openModalNow_ = true;
    RequestFrames();
}

void App::Toast(const std::string& text) {
    toast_ = text;
    toastUntil_ = (ImGui::GetCurrentContext() ? ImGui::GetTime() : 0) + 2.5;
    RequestFrames();
}

// ===========================================================================
// Fluxo de gravação
// ===========================================================================
void App::ShowWindowNormal() {
    if (IsIconic(hwnd_) || !IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    RequestFrames();
}

void App::HideForCapture() {
    ShowWindow(hwnd_, SW_HIDE);
    // espera o Windows realmente tirar a janela da tela antes de capturar
    Sleep(150);
    DwmFlush();
}

void App::OpenRecordChooser() {
    page_ = Page::Home;
    openRecordMenu_ = true;
    ShowWindowNormal();
}

void App::GoFullScreenPicker() {
    RefreshMonitors(true);
    pickedMonitor_ = 0;
    for (size_t i = 0; i < monitors_.size(); i++) {
        if (Narrow(monitors_[i].deviceName) == settings_.lastMonitorDevice) { pickedMonitor_ = (int)i; break; }
        if (monitors_[i].primary) pickedMonitor_ = (int)i;
    }
    page_ = Page::Monitors;
    RequestFrames();
}

void App::BeginRegionSelection() {
    if (selector_.Active()) return;
    if (recorder_->IsBusy()) {
        Toast("Já existe uma gravação em andamento.");
        return;
    }
    HideForCapture();
    RefreshMonitors(false);
    bool ok = selector_.Begin(monitors_, {hwnd_}, [this](bool confirmed, RECT r, int mon) {
        if (!confirmed || mon < 0 || mon >= (int)monitors_.size()) {
            ShowWindowNormal();
            Toast("Seleção de área cancelada.");
            return;
        }
        CaptureTarget t;
        t.fullScreen = false;
        t.monitor = monitors_[(size_t)mon];
        t.region = r;
        StartCapture(t, false);
    });
    if (!ok) {
        ShowWindowNormal();
        ShowError("Seleção de área", "Não foi possível abrir o seletor de área. Veja o log para detalhes.");
    }
}

void App::StartCapture(const CaptureTarget& t, bool fromHotkey) {
    (void)fromHotkey;
    if (recorder_->IsBusy() || finalizing_) {
        ShowWindowNormal();
        Toast(finalizing_ ? "Aguarde o vídeo anterior terminar de ser processado." : "Já existe uma gravação em andamento.");
        return;
    }
    if (!capsReady_) {
        // ainda detectando ffmpeg/codificadores (só nos primeiros segundos após abrir)
        pendingStart_ = true;
        pendingTarget_ = t;
        Toast("Preparando os codificadores de vídeo…");
        return;
    }
    DoStartCapture(t);
}

void App::DoStartCapture(const CaptureTarget& t) {
    currentTarget_ = t;
    settings_.lastMonitorDevice = Narrow(t.monitor.deviceName);
    SaveSettings(settings_, nullptr);
    // a janela principal não pode aparecer no vídeo
    if (t.fullScreen && IsWindowVisible(hwnd_) && !IsIconic(hwnd_)) HideForCapture();

    auto reallyStart = [this, t]() {
        std::string err;
        if (!recorder_->Start(t, settings_, &err)) {
            ShowWindowNormal();
            ShowError("Não foi possível gravar", err);
            return;
        }
        recordingUi_ = true;
        // A janela principal também some do vídeo (se for reaberta durante a
        // gravação, aparece só para você). Sem esse recurso no Windows, ela é
        // minimizada sempre que estiver em cima da área gravada.
        mainExcluded_ = IsCaptureExclusionSupported() && ExcludeWindowFromCapture(hwnd_);
        bool mustHide = false;
        if (!mainExcluded_) {
            RECT wr{}, inter{};
            RECT cap = t.fullScreen ? t.monitor.rect : t.region;
            GetWindowRect(hwnd_, &wr);
            mustHide = IntersectRect(&inter, &wr, &cap) != FALSE;
        }
        Log::Info("Janela principal %s da captura.", mainExcluded_ ? "excluída" : "NÃO excluída");
        if (settings_.minimizeOnRecord || mustHide) ShowWindow(hwnd_, SW_SHOWMINNOACTIVE);
        else ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        PlaceControlBar(t);
        if (!t.fullScreen && settings_.showRegionBorder) border_.Show(t.region, t.monitor.dpi);
        ApplyHotkeys(true);
        tray_.SetIcon(iconRec_);
        page_ = Page::Home;
        RequestFrames();
    };
    if (settings_.countdown > 0) {
        Log::Info("Contagem regressiva de %d s", settings_.countdown);
        countdown_.Start(settings_.countdown, t.monitor.rect, t.monitor.dpi, [this, reallyStart](bool ok) {
            if (ok) {
                reallyStart();
            } else {
                ShowWindowNormal();
                Toast("Gravação cancelada.");
            }
        });
    } else {
        DwmFlush();   // garante que a janela do seletor já sumiu da tela
        reallyStart();
    }
}

void App::PlaceControlBar(const CaptureTarget& t) {
    bar_.onPauseToggle = [this] { recorder_->TogglePause(); RequestFrames(); };
    bar_.onStop = [this] { recorder_->Stop(); RequestFrames(); };
    bar_.onMicToggle = [this] { ToggleMicMute(); };
    const MonitorInfo& m = t.monitor;
    if (!bar_.Show(m.work, m.dpi, recorder_->HasMic())) return;
    if (bar_.ExcludedFromCapture()) return;
    char forceBar[4] = {0};
    if (GetEnvironmentVariableA("SCP_FORCE_BAR", forceBar, sizeof(forceBar)) && forceBar[0] == '1') return;   // só para testes
    // ---- plano B (Windows sem WDA_EXCLUDEFROMCAPTURE): tira a barra de cima da área gravada
    RECT cap = t.fullScreen ? m.rect : t.region;
    RECT br = bar_.WindowRect(), inter;
    int bw = br.right - br.left, bh = br.bottom - br.top, gap = (int)(10 * m.dpi / 96);
    if (!IntersectRect(&inter, &br, &cap)) return;
    std::vector<POINT> tries;
    if (!t.fullScreen) {
        tries.push_back({cap.left, cap.bottom + gap});
        tries.push_back({cap.left, cap.top - bh - gap});
        tries.push_back({cap.right + gap, cap.top});
        tries.push_back({cap.left - bw - gap, cap.top});
    }
    for (auto& other : monitors_)
        if (other.deviceName != m.deviceName) tries.push_back({other.work.left + gap, other.work.top + gap});
    for (auto& p : tries) {
        RECT nr = {p.x, p.y, p.x + bw, p.y + bh};
        bool onScreen = false;
        for (auto& mm : monitors_) {
            RECT tmp;
            if (IntersectRect(&tmp, &nr, &mm.work) && EqualRect(&tmp, &nr)) onScreen = true;
        }
        if (onScreen && !IntersectRect(&inter, &nr, &cap)) {
            bar_.MoveTo(p.x, p.y);
            return;
        }
    }
    bar_.Hide();
    std::string msg = "Este Windows não permite esconder a barra de controle do vídeo, e não há espaço fora da área gravada. "
                      "Use " + HotkeyText(settings_.hkPause) + " para pausar, " + HotkeyText(settings_.hkStop) +
                      " para finalizar, ou o ícone perto do relógio.";
    tray_.Notify(APP_NAME_U8, msg);
    AddNotice(Notice::Kind::Info, msg);
}

void App::TearDownRecordingUi() {
    bar_.Hide();
    border_.Hide();
    countdown_.Cancel();
    if (mainExcluded_) {
        IncludeWindowInCapture(hwnd_);
        mainExcluded_ = false;
    }
    recordingUi_ = false;
    ApplyHotkeys(false);
    tray_.SetIcon(iconAppSmall_);
}

void App::OnRecorderEvent(const RecorderEvent& e) {
    using T = RecorderEvent::Type;
    switch (e.type) {
        case T::Started:
            Log::Info("Gravação iniciada.");
            break;
        case T::Paused:
            border_.SetPaused(true);
            break;
        case T::Resumed:
            border_.SetPaused(false);
            break;
        case T::Info:
            AddNotice(Notice::Kind::Info, e.text);
            if (!IsWindowVisible(hwnd_) || IsIconic(hwnd_)) tray_.Notify(APP_NAME_U8, e.text);
            break;
        case T::Warning:
        case T::AudioNotice:
            AddNotice(Notice::Kind::Warning, e.text);
            if (!IsWindowVisible(hwnd_) || IsIconic(hwnd_)) tray_.Notify(APP_NAME_U8, e.text);
            break;
        case T::Error:
            if (e.fatal) {
                if (!recorder_->IsBusy()) TearDownRecordingUi();
                finalizing_ = recorder_->State() == RecState::Finalizing;
                ShowWindowNormal();
                ShowError("Problema na gravação", e.text);
                tray_.Notify("Problema na gravação", e.text, true);
            } else {
                AddNotice(Notice::Kind::Error, e.text);
            }
            break;
        case T::Finalizing:
            TearDownRecordingUi();
            finalizing_ = true;
            finalizeProgress_ = 0;
            finalizeStage_ = "Processando o vídeo";
            break;
        case T::Progress:
            finalizeProgress_ = e.progress;
            finalizeStage_ = e.text;
            break;
        case T::Finished: {
            finalizing_ = false;
            lastFile_ = e.path;
            std::string name = Narrow(FileNameOf(e.path));
            Notice n;
            n.kind = Notice::Kind::Success;
            n.text = Fmt("Gravação salva: %s  (%s, %s)", name.c_str(), FormatDuration(e.duration, false).c_str(), FormatBytes(e.size).c_str());
            n.file = e.path;
            notices_.insert(notices_.begin(), n);
            if (notices_.size() > 6) notices_.pop_back();
            tray_.Notify("Vídeo pronto!", name + " — " + FormatBytes(e.size) + ". Clique para abrir a pasta.");
            library_.Refresh(OutputDir());
            RefreshDisk();
            AfterFinished(e.path);
            if (quitAfterFinalize_) DestroyWindow(hwnd_);
            break;
        }
        case T::Discarded:
            TearDownRecordingUi();
            finalizing_ = false;
            AddNotice(Notice::Kind::Info, e.text);
            ShowWindowNormal();
            break;
    }
    RequestFrames();
}

void App::AfterFinished(const std::wstring& file) {
    switch (settings_.afterFinish) {
        case AfterFinish::OpenFolder: RevealFileInExplorer(file); break;
        case AfterFinish::OpenVideo: OpenWithDefaultApp(file); break;
        case AfterFinish::ShowLibrary:
            page_ = Page::Library;
            ShowWindowNormal();
            break;
        case AfterFinish::Notify:
        case AfterFinish::Nothing:
            if (!settings_.minimizeOnRecord) ShowWindowNormal();
            break;
    }
}

void App::ToggleMicMute() {
    if (!recorder_->HasMic()) return;
    recorder_->SetMicMuted(!recorder_->MicMuted());
    lastTrayTip_ = 0;   // atualiza a dica do ícone da bandeja já
    RequestFrames();
}

void App::ApplyHotkeys(bool notifyFailures) {
    bool rec = recordingUi_ || (recorder_ && recorder_->IsBusy() && recorder_->State() != RecState::Finalizing);
    auto fails = hotkeys_.Apply(settings_, rec, rec && recorder_ && recorder_->HasMic());
    if (notifyFailures)
        for (auto& f : fails) AddNotice(Notice::Kind::Warning, "Não foi possível ativar o atalho " + f + ". Troque-o em Configurações > Atalhos.", 2, "Trocar atalho");
}

void App::ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    RecState st = recorder_->State();
    bool busy = recorder_->IsBusy() || finalizing_;
    if (!busy) {
        AppendMenuW(m, MF_STRING, TC_FULL, L"Gravar tela inteira…");
        AppendMenuW(m, MF_STRING, TC_REGION, L"Gravação livre (selecionar área)");
    } else {
        AppendMenuW(m, MF_STRING | (st == RecState::Recording || st == RecState::Paused ? 0 : MF_GRAYED), TC_PAUSE,
                    st == RecState::Paused ? L"Continuar" : L"Pausar");
        AppendMenuW(m, MF_STRING | (st == RecState::Recording || st == RecState::Paused ? 0 : MF_GRAYED), TC_STOP, L"Finalizar gravação");
        if (recorder_->HasMic())
            AppendMenuW(m, MF_STRING, TC_MIC, recorder_->MicMuted() ? L"Reativar o microfone" : L"Silenciar o microfone");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, TC_SHOW, L"Abrir o " APP_NAME_W);
    AppendMenuW(m, MF_STRING, TC_FOLDER, L"Abrir pasta de gravações");
    AppendMenuW(m, MF_STRING, TC_LIBRARY, L"Ver gravações");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, TC_EXIT, L"Sair");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);   // exigência do Windows para o menu fechar direito
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) HandleTrayCommand(cmd);
}

void App::HandleTrayCommand(int cmd) {
    switch (cmd) {
        case TC_FULL: ShowWindowNormal(); GoFullScreenPicker(); break;
        case TC_REGION: BeginRegionSelection(); break;
        case TC_PAUSE: recorder_->TogglePause(); break;
        case TC_STOP: recorder_->Stop(); break;
        case TC_MIC: ToggleMicMute(); break;
        case TC_SHOW: ShowWindowNormal(); break;
        case TC_FOLDER: EnsureDir(OutputDir()); OpenFolderInExplorer(OutputDir()); break;
        case TC_LIBRARY: page_ = Page::Library; library_.Refresh(OutputDir()); ShowWindowNormal(); break;
        case TC_EXIT: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
    }
    RequestFrames();
}

void App::StartRecovery(const std::wstring& dir) {
    if (finalizing_) return;
    finalizing_ = true;
    recovering_ = true;
    finalizeProgress_ = 0;
    finalizeStage_ = "Recuperando gravação";
    if (perfThread_.joinable()) perfThread_.join();
    std::wstring fallback = OutputDir();
    perfThread_ = std::thread([this, dir, fallback] {
        ScopedCom com;
        Session s;
        std::string err;
        FinalizeResult r;
        if (!Session::Load(dir, s, &err)) r.error = err;
        else r = FinalizeSession(s, fallback, [this](float f, const std::string& stage) {
            PostUi([this, f, stage] { finalizeProgress_ = f; finalizeStage_ = "Recuperando: " + stage; });
        });
        if (r.ok || r.nothingRecorded) DeleteDirectoryTree(dir);
        PostUi([this, r] {
            finalizing_ = false;
            recovering_ = false;
            if (r.ok) {
                lastFile_ = r.outputFile;
                Notice n;
                n.kind = Notice::Kind::Success;
                n.text = "Gravação recuperada: " + Narrow(FileNameOf(r.outputFile)) + "  (" + FormatDuration(r.duration, false) + ")";
                n.file = r.outputFile;
                notices_.insert(notices_.begin(), n);
                library_.Refresh(OutputDir());
            } else if (r.nothingRecorded) {
                AddNotice(Notice::Kind::Info, "A gravação interrompida não tinha nenhum quadro aproveitável e foi removida.");
            } else {
                ShowError("Não foi possível recuperar", r.error + "\n\nOs arquivos temporários foram mantidos.");
            }
            if (quitAfterFinalize_ && (r.ok || r.nothingRecorded)) DestroyWindow(hwnd_);
        });
    });
}
