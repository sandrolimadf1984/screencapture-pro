#pragma once
#include "common.h"

// ---------------------------------------------------------------------------
// Captura de áudio pela WASAPI.
//  - Áudio do sistema: "loopback" do dispositivo de saída (tudo que toca no PC)
//  - Microfone: dispositivo de entrada escolhido
// Cada fonte roda numa thread própria e grava PCM 16 bits, 48 kHz, estéreo
// (o processamento interno é em float), ALINHADO AO RELÓGIO: a amostra n do arquivo corresponde exatamente
// ao instante (início do trecho + n/48000). Silêncios (o loopback não entrega
// nada quando nada toca) viram zeros, então o áudio nunca "anda" em relação ao
// vídeo, nem depois de pausar e continuar.
// ---------------------------------------------------------------------------

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kTempBytesPerFrame = kAudioChannels * 2;   // arquivo temporário: s16le estéreo

struct AudioDevice {
    std::string id;       // ID do endpoint (string do Windows)
    std::string name;     // "Alto-falantes (Realtek(R) Audio)"
};

struct AudioDeviceList {
    bool ok = false;
    std::string error;
    std::vector<AudioDevice> devices;
    std::string defaultId, defaultName;
};

// render = true -> saídas (para loopback); false -> microfones
AudioDeviceList ListAudioDevices(bool render);

enum class AudioKind { SystemLoopback, Microphone, TestTone };

class AudioCapture {
public:
    enum class Status { Stopped, Starting, Running, Reconnecting, Failed };

    AudioCapture(AudioKind kind, std::string deviceId);
    ~AudioCapture();

    bool Start();                                   // inicia a thread (não bloqueia)
    void Stop();

    // Início/fim de um trecho gravado. segStartWallUs = instante (relógio do
    // sistema) que corresponde à amostra 0 do arquivo.
    bool BeginSegment(const std::wstring& path, int64_t segStartWallUs, std::string* error);
    int64_t EndSegment();                           // devolve amostras gravadas

    float TakePeak();                               // pico desde a última leitura (0..1)

    // Mudo: a captura continua, mas o que vai para o arquivo vira silêncio
    // (com uma rampa de 10 ms para não dar estalo). Vale até ser desligado,
    // inclusive depois de pausar e continuar.
    void SetMuted(bool m) { muted_ = m; }
    bool Muted() const { return muted_.load(); }
    Status GetStatus() const { return status_.load(); }
    std::string StatusText() const;
    std::string LastError() const;                  // explicação do último erro (em português)
    std::string DeviceName() const;
    AudioKind Kind() const { return kind_; }

    // Mensagens para a interface (dispositivo desconectado, reconectado...)
    std::function<void(const std::string&)> onEvent;

private:
    void ThreadMain();
    // err = detalhe técnico (log); friendly = explicação em português para a tela
    bool OpenDevice(std::string* err, std::string* friendly);
    void CloseDevice();
    bool DefaultDeviceChanged();                    // o "padrão do Windows" mudou (fone conectado etc.)?
    bool Poll();                                    // false = dispositivo perdido
    void HandlePacket(const BYTE* data, UINT32 frames, DWORD flags, UINT64 qpc100ns);
    void ToStereoFloat(const BYTE* data, UINT32 frames, std::vector<float>& out);
    void Resample(const std::vector<float>& in, std::vector<float>& out, double& firstOutOffsetFrames);
    void Deliver(const float* stereo, size_t frames, int64_t wallUs);   // grava alinhado
    void PadSilence(int64_t nowWallUs);
    void WriteSamples(const float* data, size_t frames);
    void WriteZeros(size_t frames);
    void FlushFile(bool force);
    void Emit(const std::string& msg);
    void GenerateTestTone();

    AudioKind kind_;
    std::string deviceId_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<Status> status_{Status::Stopped};
    mutable std::mutex nameMtx_;
    std::string deviceName_, lastError_;
    std::atomic<float> peak_{0.0f};
    std::atomic<bool> muted_{false};
    float gain_ = 1.0f;          // ganho atual da rampa do mudo (protegido por segMtx_)
    std::vector<float> rampBuf_;

    // WASAPI (só usados na thread de captura)
    void* client_ = nullptr;     // IAudioClient*
    void* capture_ = nullptr;    // IAudioCaptureClient*
    void* enumerator_ = nullptr; // IMMDeviceEnumerator* (vida inteira da thread)
    std::string openedId_;       // ID do dispositivo aberto agora
    bool usingDefault_ = false;  // aberto como "padrão do Windows" (acompanha as trocas)
    int64_t lastDefaultCheckUs_ = 0;
    int srcRate_ = 48000, srcChannels_ = 2, srcBits_ = 32, srcContainerBits_ = 32;
    bool srcFloat_ = true;
    DWORD channelMask_ = 0;
    ClockPair clock_;
    int64_t lastClockRefreshUs_ = 0;
    int64_t lastDataWallUs_ = 0;
    int64_t expectNextWallUs_ = 0;   // para pacotes sem timestamp

    // reamostragem (quando a placa não está em 48 kHz)
    double rsPos_ = 0;
    std::vector<float> rsHist_;

    // gravação do trecho (protegido por segMtx_)
    std::mutex segMtx_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    int64_t segStartUs_ = 0;
    int64_t written_ = 0;
    std::vector<char> fileBuf_;
    int64_t lastFlushUs_ = 0;
    int64_t testToneNextUs_ = 0;
    std::vector<float> tmpA_, tmpB_;
    std::vector<int16_t> pcm16_;
};
