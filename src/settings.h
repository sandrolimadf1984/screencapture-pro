#pragma once
#include "common.h"

// ---------------------------------------------------------------------------
// Todas as opções do usuário. São salvas em JSON (config\settings.json no
// modo portátil, ou %APPDATA%\ScreenCapturePro\settings.json no modo normal).
// ---------------------------------------------------------------------------

enum class Container { MP4 = 0, AVI = 1 };
enum class VideoCodec { H264 = 0, HEVC = 1, MPEG4 = 2, MJPEG = 3 };
enum class EncoderPref { Auto = 0, Software = 1, NVENC = 2, QSV = 3, AMF = 4 };
enum class QualityPreset { Recommended = 0, Small = 1, High = 2, CustomBitrate = 3 };
enum class ResolutionPreset { Original = 0, P360 = 1, P480 = 2, P720 = 3, P1080 = 4, Custom2060 = 5, UHD4K = 6 };
enum class AspectMode { Letterbox = 0, Fit = 1, Stretch = 2 };
enum class CaptureMethod { Auto = 0, DDA = 1, GDI = 2 };
enum class AudioTracks { Mixed = 0, Separate = 1 };
enum class AviAudio { MP3 = 0, PCM = 1 };
enum class AfterFinish { Notify = 0, OpenFolder = 1, OpenVideo = 2, ShowLibrary = 3, Nothing = 4 };
enum class StartAction { ShowChooser = 0, FullScreen = 1, Region = 2 };

struct Hotkey {
    bool enabled = true;
    UINT mods = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk = 0;
    bool operator==(const Hotkey& o) const { return enabled == o.enabled && mods == o.mods && vk == o.vk; }
};

struct Settings {
    // ---- vídeo
    Container container = Container::MP4;
    VideoCodec codecMp4 = VideoCodec::H264;
    VideoCodec codecAvi = VideoCodec::MPEG4;
    EncoderPref encoder = EncoderPref::Auto;
    QualityPreset quality = QualityPreset::Recommended;
    int customBitrateKbps = 8000;
    ResolutionPreset resolution = ResolutionPreset::Original;
    int customW = 3662, customH = 2060;     // "2060p": largura e altura livres
    AspectMode aspect = AspectMode::Letterbox;
    int fps = 30;
    bool drawMouse = true;
    CaptureMethod captureMethod = CaptureMethod::Auto;

    // ---- áudio
    bool sysAudio = true;
    std::string sysDeviceId;        // vazio = dispositivo padrão do Windows
    std::string sysDeviceName;
    int sysVolume = 100;            // % (0 a 300)
    bool mic = false;
    std::string micDeviceId;
    std::string micDeviceName;
    int micVolume = 100;
    AudioTracks tracks = AudioTracks::Mixed;
    int aacBitrate = 160;           // kbps (MP4)
    AviAudio aviAudio = AviAudio::MP3;
    int mp3Bitrate = 192;           // kbps (AVI)

    // ---- gravações
    std::string outputDir;          // UTF-8; vazio = pasta padrão
    std::string namePattern = APP_FILE_PREFIX "_{data}_{hora}";
    AfterFinish afterFinish = AfterFinish::Notify;
    int countdown = 3;              // segundos (0 = desligado)
    bool minimizeOnRecord = true;
    bool showRegionBorder = true;

    // ---- atalhos
    Hotkey hkStart{true, 0, VK_F9};
    Hotkey hkPause{true, 0, VK_F10};
    Hotkey hkStop{true, 0, VK_F11};
    Hotkey hkMic{true, 0, VK_F8};    // silenciar / reativar o microfone durante a gravação
    StartAction startAction = StartAction::ShowChooser;

    // ---- memória da última escolha
    std::string lastMonitorDevice;  // ex.: \\.\DISPLAY1

    // Codec efetivo do contêiner escolhido
    VideoCodec CurrentCodec() const { return container == Container::MP4 ? codecMp4 : codecAvi; }
    void Sanitize();               // corrige valores fora da faixa
};

// Descrições curtas para a interface
const char* ContainerName(Container c);
const char* CodecName(VideoCodec c);
const char* CodecLongName(VideoCodec c);
const char* QualityName(QualityPreset q);
const char* ResolutionName(ResolutionPreset r);
bool ResolutionSize(const Settings& s, int& w, int& h);   // false = "Original"
std::string ResolutionLabel(const Settings& s);
const char* AfterFinishName(AfterFinish a);
std::string HotkeyText(const Hotkey& h);
std::wstring ContainerExtension(Container c);

// Leitura/gravação no disco
bool LoadSettings(Settings& s, std::string* warning);
bool SaveSettings(const Settings& s, std::string* error);
std::wstring SettingsFilePath();
