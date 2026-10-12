#pragma once
#include <d3d11.h>

#include "audio.h"
#include "common.h"
#include "controlbar.h"
#include "ffmpeg.h"
#include "hotkeys.h"
#include "library.h"
#include "monitors.h"
#include "overlay.h"
#include "recorder.h"
#include "session.h"
#include "settings.h"
#include "tray.h"

#define WM_APP_WAKE (WM_APP + 1)
#define WM_APP_TRAY (WM_APP + 2)

// Avisos que aparecem na tela inicial
struct Notice {
    enum class Kind { Info, Success, Warning, Error } kind = Kind::Info;
    std::string text;
    std::wstring file;          // sucesso: arquivo gravado (botões Abrir / Mostrar na pasta)
    std::string action;         // texto de um botão extra ("Escolher outro dispositivo")
    int actionId = 0;
    double time = 0;
};

// Miniatura de monitor (textura da GPU para a tela "Tela inteira")
struct MonitorThumb {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
};

class App {
public:
    int Run(HINSTANCE inst, int show);

private:
    enum class Page { Home, Monitors, Settings, Library };
    enum class Modal { None, Error, ConfirmDelete, ConfirmDiscard, ConfirmExit, Recovery, FolderMissing, About };

    // ---- janela e Direct3D
    bool CreateMainWindow(int show);
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    void RenderFrame();
    void OnDpiChanged(UINT dpi);

    // ---- ciclo de vida
    void Startup();
    void Shutdown();
    void Tick();
    void PostUi(std::function<void()> fn);
    void DrainUi();
    void RequestFrames(int n = 3) { framesPending_ = std::max(framesPending_, n); }
    bool Animating() const;

    // ---- fluxo de gravação
    void OpenRecordChooser();
    void GoFullScreenPicker();
    void BeginRegionSelection();
    void StartCapture(const CaptureTarget& t, bool fromHotkey);
    void DoStartCapture(const CaptureTarget& t);
    void OnRecorderEvent(const RecorderEvent& e);
    void TearDownRecordingUi();
    void ShowWindowNormal();
    void HideForCapture();
    void PlaceControlBar(const CaptureTarget& t);
    void ApplyHotkeys(bool notifyFailures);
    void ToggleMicMute();
    void ShowTrayMenu();
    void HandleTrayCommand(int cmd);
    void AfterFinished(const std::wstring& file);
    void StartRecovery(const std::wstring& sessionDir);

    // ---- avisos
    void AddNotice(Notice::Kind k, const std::string& text, int actionId = 0, const std::string& action = "");
    void ShowError(const std::string& title, const std::string& text);
    void Toast(const std::string& text);

    // ---- dados auxiliares
    void RefreshMonitors(bool thumbs);
    void ReleaseThumbs();
    void RefreshAudioDevices();
    void RefreshDisk();
    std::wstring OutputDir() const;
    void CheckOutputFolder();
    void WarnIfFat32();
    void CheckAudioDevices();
    void StartMonitoring();
    void StopMonitoring();

    // ---- páginas (ui_pages.cpp)
    void DrawRoot();
    void DrawHeader(const char* title, bool back);
    void DrawHome();
    void DrawRecordPanel();
    void DrawMonitors();
    void DrawSettings();
    void DrawSettingsVideo();
    void DrawSettingsAudio();
    void DrawSettingsFiles();
    void DrawSettingsHotkeys();
    void DrawSettingsAdvanced();
    void DrawLibrary();
    void DrawNotices();
    void DrawModals();
    void DrawToast();
    void OpenSettings(int tab);
    void SaveSettingsFromEditor();
    bool HotkeyCaptureWidget(const char* id, Hotkey* hk, int slot);
    void RunPerformanceTest();

    // ---- estado
    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON iconApp_ = nullptr, iconRec_ = nullptr, iconAppSmall_ = nullptr;
    ID3D11Device* dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    IDXGISwapChain* swap_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    UINT resizeW_ = 0, resizeH_ = 0;
    float dpiScale_ = 1.0f;
    bool pendingDpi_ = false;
    int framesPending_ = 3;
    bool running_ = true;
    bool quitAfterFinalize_ = false;
    UINT taskbarCreatedMsg_ = 0;

    Settings settings_;
    Settings edit_;
    int settingsTab_ = 0;
    Page page_ = Page::Home;
    Modal modal_ = Modal::None;
    bool openModalNow_ = false;
    std::string modalTitle_, modalText_;
    std::wstring modalFile_;
    std::vector<std::wstring> pendingSessions_;

    std::unique_ptr<Recorder> recorder_;
    Library library_;
    RegionSelector selector_;
    ControlBar bar_;
    RegionBorder border_;
    Countdown countdown_;
    TrayIcon tray_;
    Hotkeys hotkeys_;

    std::vector<MonitorInfo> monitors_;
    std::vector<MonitorThumb> thumbs_;
    bool releaseThumbs_ = false;   // liberar texturas só depois de desenhar o quadro
    int pickedMonitor_ = 0;
    CaptureTarget currentTarget_;
    bool recordingUi_ = false;
    bool mainExcluded_ = false;    // janela principal excluída da captura durante a gravação

    AudioDeviceList outDevs_, inDevs_;
    std::unique_ptr<AudioCapture> monSys_, monMic_;   // monitoramento (medidores) na tela de configurações
    std::string monSysId_, monMicId_;
    bool monSysOn_ = false, monMicOn_ = false;
    float sysMeter_ = 0, micMeter_ = 0;

    uint64_t diskFree_ = 0, diskTotal_ = 0;
    double lastDiskCheck_ = -100;
    double lastTick_ = 0;
    double lastTrayTip_ = 0;

    std::mutex uiMtx_;
    std::vector<std::function<void()>> uiQueue_;
    std::vector<Notice> notices_;
    std::string toast_;
    double toastUntil_ = 0;
    bool openRecordMenu_ = false;
    std::string lastStatus_;
    float finalizeProgress_ = 0;
    std::string finalizeStage_;
    bool finalizing_ = false;
    bool recovering_ = false;
    std::wstring lastFile_;
    std::wstring deleteTarget_;
    char librarySearch_[128] = {0};

    // atalhos: captura de tecla na tela de configurações
    int capturingHotkey_ = 0;
    Hotkey capturedHotkey_;
    bool hotkeyCaptured_ = false;

    // teste de desempenho
    std::atomic<bool> perfRunning_{false};
    std::string perfResult_;
    std::mutex perfMtx_;
    std::thread perfThread_;

    // capacidades (detectadas em segundo plano)
    std::thread probeThread_;
    std::atomic<bool> capsReady_{false};
    bool pendingStart_ = false;
    CaptureTarget pendingTarget_;
};
