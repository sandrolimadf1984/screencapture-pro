#include "audio.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>
#include <algorithm>
#include <cmath>

#include "log.h"

// GUIDs definidos aqui mesmo (evita depender de bibliotecas extras do MinGW)
static const CLSID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID kIID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID kIID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID kIID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
static const PROPERTYKEY kPKEY_Device_FriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
static const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static std::string DeviceFriendlyName(IMMDevice* dev) {
    std::string name;
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(kPKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) name = Narrow(v.pwszVal);
        PropVariantClear(&v);
        props->Release();
    }
    return name.empty() ? "Dispositivo sem nome" : name;
}

static std::string DeviceIdOf(IMMDevice* dev) {
    LPWSTR id = nullptr;
    std::string s;
    if (SUCCEEDED(dev->GetId(&id)) && id) s = Narrow(id);
    if (id) CoTaskMemFree(id);
    return s;
}

AudioDeviceList ListAudioDevices(bool render) {
    AudioDeviceList out;
    ScopedCom com;
    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en);
    if (FAILED(hr)) {
        out.error = "Serviço de áudio do Windows indisponível (" + HResultText(hr) + ")";
        return out;
    }
    EDataFlow flow = render ? eRender : eCapture;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &def)) && def) {
        out.defaultId = DeviceIdOf(def);
        out.defaultName = DeviceFriendlyName(def);
        def->Release();
    }
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; i++) {
            IMMDevice* d = nullptr;
            if (SUCCEEDED(col->Item(i, &d)) && d) {
                out.devices.push_back({DeviceIdOf(d), DeviceFriendlyName(d)});
                d->Release();
            }
        }
        col->Release();
    }
    en->Release();
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------------------
AudioCapture::AudioCapture(AudioKind kind, std::string deviceId) : kind_(kind), deviceId_(std::move(deviceId)) {
    fileBuf_.reserve(1 << 18);
}

AudioCapture::~AudioCapture() {
    Stop();
}

bool AudioCapture::Start() {
    if (thread_.joinable()) return true;
    stop_ = false;
    status_ = Status::Starting;
    thread_ = std::thread([this] { ThreadMain(); });
    return true;
}

void AudioCapture::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    EndSegment();
    status_ = Status::Stopped;
}

std::string AudioCapture::DeviceName() const {
    std::lock_guard<std::mutex> lk(nameMtx_);
    return deviceName_;
}

std::string AudioCapture::StatusText() const {
    switch (status_.load()) {
        case Status::Stopped: return "parado";
        case Status::Starting: return "iniciando";
        case Status::Running: return "ok";
        case Status::Reconnecting: return "dispositivo desconectado — tentando reconectar";
        case Status::Failed: {
            std::lock_guard<std::mutex> lk(nameMtx_);
            return "falhou: " + lastError_;
        }
    }
    return "?";
}

std::string AudioCapture::LastError() const {
    std::lock_guard<std::mutex> lk(nameMtx_);
    return lastError_;
}

float AudioCapture::TakePeak() { return peak_.exchange(0.0f); }

void AudioCapture::Emit(const std::string& msg) {
    Log::Info("Áudio (%s): %s", kind_ == AudioKind::Microphone ? "microfone" : "sistema", msg.c_str());
    if (onEvent) onEvent(msg);
}

// Explicação em português para os erros mais comuns da WASAPI
static std::string FriendlyAudioError(HRESULT hr, bool mic) {
    switch (hr) {
        case E_ACCESSDENIED:
            return mic ? "o Windows bloqueou o acesso ao microfone. Abra Configurações > Privacidade e segurança > Microfone e "
                         "ative \"Permitir que aplicativos da área de trabalho acessem o microfone\""
                       : "o Windows negou o acesso ao dispositivo de áudio";
        case AUDCLNT_E_DEVICE_IN_USE: return "o dispositivo está sendo usado em modo exclusivo por outro programa";
        case AUDCLNT_E_DEVICE_INVALIDATED: return "o dispositivo foi desconectado";
        case AUDCLNT_E_SERVICE_NOT_RUNNING: return "o serviço de áudio do Windows não está em execução";
        case AUDCLNT_E_UNSUPPORTED_FORMAT: return "o dispositivo usa um formato de áudio não suportado";
        case AUDCLNT_E_CPUUSAGE_EXCEEDED: return "o Windows recusou o áudio por excesso de uso do processador";
        case AUDCLNT_E_WRONG_ENDPOINT_TYPE: return "este dispositivo não permite capturar o som que ele reproduz";
        case AUDCLNT_E_ENDPOINT_CREATE_FAILED: return "o Windows não conseguiu preparar o dispositivo de áudio";
    }
    return "erro do Windows " + Fmt("0x%08lX", (unsigned long)hr);
}

bool AudioCapture::OpenDevice(std::string* err, std::string* friendly) {
    CloseDevice();
    bool loop = kind_ == AudioKind::SystemLoopback;
    bool mic = kind_ == AudioKind::Microphone;
    auto fail = [&](const std::string& tech, const std::string& nice) {
        if (err) *err = tech;
        if (friendly) *friendly = nice;
        return false;
    };
    auto* en = static_cast<IMMDeviceEnumerator*>(enumerator_);
    if (!en) {
        HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en);
        if (FAILED(hr)) return fail("enumerador de áudio: " + HResultText(hr), "o serviço de áudio do Windows não respondeu");
        enumerator_ = en;
    }
    IMMDevice* dev = nullptr;
    HRESULT hr = S_OK;
    usingDefault_ = false;
    if (!deviceId_.empty()) hr = en->GetDevice(Widen(deviceId_).c_str(), &dev);
    if (deviceId_.empty() || FAILED(hr) || !dev) {
        if (!deviceId_.empty()) Log::Warn("Dispositivo salvo não encontrado (%s); usando o padrão.", deviceId_.c_str());
        dev = nullptr;
        hr = en->GetDefaultAudioEndpoint(loop ? eRender : eCapture, eConsole, &dev);
        usingDefault_ = true;
    }
    if (FAILED(hr) || !dev) {
        const char* what = loop ? "nenhum alto-falante/fone (dispositivo de saída) está ativo" : "nenhum microfone está conectado";
        return fail(what, what);
    }
    openedId_ = DeviceIdOf(dev);
    {
        std::lock_guard<std::mutex> lk(nameMtx_);
        deviceName_ = DeviceFriendlyName(dev);
    }
    IAudioClient* client = nullptr;
    hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&client);
    dev->Release();
    if (FAILED(hr)) return fail("Activate: " + HResultText(hr), FriendlyAudioError(hr, mic));

    WAVEFORMATEX* wfx = nullptr;
    hr = client->GetMixFormat(&wfx);
    if (FAILED(hr) || !wfx) {
        client->Release();
        return fail("GetMixFormat: " + HResultText(hr), FriendlyAudioError(hr, mic));
    }
    srcRate_ = (int)wfx->nSamplesPerSec;
    srcChannels_ = wfx->nChannels;
    srcContainerBits_ = wfx->wBitsPerSample;
    srcBits_ = wfx->wBitsPerSample;
    srcFloat_ = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    channelMask_ = 0;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx);
        srcFloat_ = IsEqualGUID(ext->SubFormat, kSubtypeFloat) != 0;
        if (ext->Samples.wValidBitsPerSample) srcBits_ = ext->Samples.wValidBitsPerSample;
        channelMask_ = ext->dwChannelMask;
    }
    // Buffer de 200 ms no modo compartilhado: folga para não perder nada.
    DWORD flags = loop ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, wfx, nullptr);
    CoTaskMemFree(wfx);
    if (FAILED(hr)) {
        client->Release();
        return fail("Initialize: " + HResultText(hr), FriendlyAudioError(hr, mic));
    }
    IAudioCaptureClient* cap = nullptr;
    hr = client->GetService(kIID_IAudioCaptureClient, (void**)&cap);
    if (FAILED(hr)) {
        client->Release();
        return fail("GetService: " + HResultText(hr), FriendlyAudioError(hr, mic));
    }
    hr = client->Start();
    if (FAILED(hr)) {
        cap->Release();
        client->Release();
        return fail("Start: " + HResultText(hr), FriendlyAudioError(hr, mic));
    }
    client_ = client;
    capture_ = cap;
    rsPos_ = 0;
    rsHist_.clear();
    Log::Info("Áudio %s aberto: \"%s\" %d Hz, %d canais, %d bits %s", loop ? "do sistema" : "do microfone", DeviceName().c_str(),
              srcRate_, srcChannels_, srcBits_, srcFloat_ ? "float" : "inteiro");
    return true;
}

void AudioCapture::CloseDevice() {
    if (client_) static_cast<IAudioClient*>(client_)->Stop();
    if (capture_) { static_cast<IAudioCaptureClient*>(capture_)->Release(); capture_ = nullptr; }
    if (client_) { static_cast<IAudioClient*>(client_)->Release(); client_ = nullptr; }
}

bool AudioCapture::DefaultDeviceChanged() {
    // Só faz sentido quando o usuário deixou "Padrão do Windows": se ele ligar um
    // fone ou trocar a saída no meio da gravação, o som passa a sair por outro
    // dispositivo — e a captura precisa ir atrás dele.
    auto* en = static_cast<IMMDeviceEnumerator*>(enumerator_);
    if (!usingDefault_ || !en) return false;
    IMMDevice* def = nullptr;
    bool changed = false;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(kind_ == AudioKind::SystemLoopback ? eRender : eCapture, eConsole, &def)) && def) {
        std::string id = DeviceIdOf(def);
        changed = !id.empty() && id != openedId_;
        def->Release();
    }
    return changed;
}

void AudioCapture::ThreadMain() {
    ScopedCom com;
    clock_ = ClockPair::Measure();
    lastClockRefreshUs_ = clock_.qpcUs;
    if (kind_ == AudioKind::TestTone) {
        // Fonte de teste: bipes de 1 kHz no início de cada segundo do relógio.
        // Usada só para validar a sincronia (variável de ambiente SCP_TEST_TONE=1).
        {
            std::lock_guard<std::mutex> lk(nameMtx_);
            deviceName_ = "Tom de teste";
        }
        status_ = Status::Running;
        testToneNextUs_ = WallClockUs();
        while (!stop_) {
            GenerateTestTone();
            Sleep(10);
        }
        return;
    }
    const char* what = kind_ == AudioKind::Microphone ? "microfone" : "áudio do sistema";
    std::string err, friendly;
    bool everOpened = false, failNotified = false;
    int64_t lastTry = 0;
    while (!stop_) {
        if (!client_) {
            int64_t now = QpcNowUs();
            if (now - lastTry >= 1000000 || lastTry == 0) {
                lastTry = now;
                if (OpenDevice(&err, &friendly)) {
                    if (everOpened) Emit("Dispositivo de áudio reconectado: " + DeviceName());
                    else if (failNotified) Emit(std::string("O ") + what + " começou a ser gravado: " + DeviceName());
                    everOpened = true;
                    status_ = Status::Running;
                } else if (!everOpened) {
                    bool firstTime = false;
                    {
                        std::lock_guard<std::mutex> lk(nameMtx_);
                        firstTime = lastError_ != friendly;
                        lastError_ = friendly;
                    }
                    // registra/avisa só a primeira vez; depois continua tentando em
                    // silêncio (o dispositivo pode ser conectado no meio da gravação)
                    if (firstTime) Log::Error("Não foi possível abrir o %s: %s (tentando de novo a cada segundo)", what, err.c_str());
                    if (!failNotified) {
                        failNotified = true;
                        Emit(std::string("Não foi possível gravar o ") + what + ": " + friendly +
                             ". A gravação continua sem esse som; ele entra sozinho se o problema for resolvido.");
                    }
                    status_ = Status::Failed;
                }
            }
        }
        if (client_ && !Poll()) {
            CloseDevice();
            status_ = Status::Reconnecting;
            Emit("Dispositivo de áudio desconectado (" + DeviceName() + "). A gravação continua; o áudio volta quando ele for reconectado.");
            lastTry = QpcNowUs() - 700000;   // tenta de novo daqui a ~300 ms
        }
        // a cada segundo: o "padrão do Windows" mudou? (fone/caixa Bluetooth conectados...)
        if (client_ && QpcNowUs() - lastDefaultCheckUs_ > 1000000) {
            lastDefaultCheckUs_ = QpcNowUs();
            if (DefaultDeviceChanged()) {
                if (OpenDevice(&err, &friendly)) {
                    Emit(std::string("O dispositivo padrão do Windows mudou; o ") + what + " agora vem de: " + DeviceName());
                } else {
                    status_ = Status::Reconnecting;
                    Log::Warn("Troca de dispositivo padrão falhou (%s); tentando de novo.", err.c_str());
                    lastTry = QpcNowUs();
                }
            }
        }
        PadSilence(WallClockUs());
        FlushFile(false);
        Sleep(10);
    }
    CloseDevice();
    if (enumerator_) {
        static_cast<IMMDeviceEnumerator*>(enumerator_)->Release();
        enumerator_ = nullptr;
    }
}

bool AudioCapture::Poll() {
    auto* cap = static_cast<IAudioCaptureClient*>(capture_);
    // o relógio do sistema pode ser ajustado pelo Windows; renovo o par QPC/relógio a cada 2 s
    int64_t q = QpcNowUs();
    if (q - lastClockRefreshUs_ > 2000000) {
        clock_ = ClockPair::Measure();
        lastClockRefreshUs_ = q;
    }
    for (int guard = 0; guard < 64; guard++) {
        UINT32 next = 0;
        HRESULT hr = cap->GetNextPacketSize(&next);
        if (FAILED(hr)) {
            Log::Warn("GetNextPacketSize: %s", HResultText(hr).c_str());
            return false;
        }
        if (next == 0) return true;
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 devPos = 0, qpcPos = 0;
        hr = cap->GetBuffer(&data, &frames, &flags, &devPos, &qpcPos);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) return true;
        if (FAILED(hr)) {
            Log::Warn("GetBuffer: %s", HResultText(hr).c_str());
            return false;
        }
        if (frames > 0) HandlePacket(data, frames, flags, qpcPos);
        cap->ReleaseBuffer(frames);
    }
    return true;
}

void AudioCapture::ToStereoFloat(const BYTE* data, UINT32 frames, std::vector<float>& out) {
    out.resize((size_t)frames * 2);
    int ch = std::max(1, srcChannels_);
    std::vector<float> f((size_t)ch);
    int bytesPer = srcContainerBits_ / 8;
    // índice de cada alto-falante (ordem padrão do WAVEFORMATEXTENSIBLE)
    int idxFC = -1, idxSL = -1, idxSR = -1, idxBL = -1, idxBR = -1, idxLFE = -1;
    if (ch > 2) {
        DWORD mask = channelMask_ ? channelMask_ : (ch == 6 ? 0x3F : (ch == 8 ? 0x63F : 0));
        int pos = 0;
        for (int bit = 0; bit < 32 && pos < ch; bit++) {
            if (!(mask & (1u << bit))) continue;
            switch (1u << bit) {
                case SPEAKER_FRONT_CENTER: idxFC = pos; break;
                case SPEAKER_LOW_FREQUENCY: idxLFE = pos; break;
                case SPEAKER_BACK_LEFT: idxBL = pos; break;
                case SPEAKER_BACK_RIGHT: idxBR = pos; break;
                case SPEAKER_SIDE_LEFT: idxSL = pos; break;
                case SPEAKER_SIDE_RIGHT: idxSR = pos; break;
            }
            pos++;
        }
    }
    (void)idxLFE;   // o subwoofer é ignorado na mistura para estéreo
    for (UINT32 i = 0; i < frames; i++) {
        const BYTE* p = data + (size_t)i * ch * bytesPer;
        for (int c = 0; c < ch; c++) {
            const BYTE* s = p + c * bytesPer;
            float v = 0;
            if (srcFloat_ && bytesPer == 4) memcpy(&v, s, 4);
            else if (bytesPer == 2) { int16_t x; memcpy(&x, s, 2); v = x / 32768.0f; }
            else if (bytesPer == 3) { int32_t x = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24); v = x / 2147483648.0f; }
            else if (bytesPer == 4) { int32_t x; memcpy(&x, s, 4); v = x / 2147483648.0f; }
            f[(size_t)c] = v;
        }
        float L, R;
        if (ch == 1) { L = R = f[0]; }
        else {
            // mistura para estéreo (coeficientes ITU, sem normalizar para não abaixar o volume)
            L = f[0]; R = f[1];
            const float k = 0.7071f;
            if (idxFC >= 0) { L += k * f[(size_t)idxFC]; R += k * f[(size_t)idxFC]; }
            if (idxSL >= 0) L += k * f[(size_t)idxSL];
            if (idxSR >= 0) R += k * f[(size_t)idxSR];
            if (idxBL >= 0) L += k * f[(size_t)idxBL];
            if (idxBR >= 0) R += k * f[(size_t)idxBR];
            L = std::clamp(L, -1.0f, 1.0f);
            R = std::clamp(R, -1.0f, 1.0f);
        }
        out[(size_t)i * 2] = L;
        out[(size_t)i * 2 + 1] = R;
    }
}

// Reamostragem cúbica (Hermite) para 48 kHz. Só entra em ação se a placa de
// som estiver configurada em outra taxa (ex.: 44,1 kHz).
void AudioCapture::Resample(const std::vector<float>& in, std::vector<float>& out, double& firstOutOffsetFrames) {
    const size_t H = 3;   // histórico: 3 quadros do pacote anterior
    if (rsHist_.empty()) rsHist_.assign(H * 2, 0.0f);
    std::vector<float>& buf = tmpB_;
    buf.clear();
    buf.insert(buf.end(), rsHist_.begin(), rsHist_.end());
    buf.insert(buf.end(), in.begin(), in.end());
    size_t total = buf.size() / 2;
    double step = (double)srcRate_ / kAudioRate;
    out.clear();
    if (rsPos_ < 1.0) rsPos_ = 1.0;
    firstOutOffsetFrames = rsPos_ - H;
    while (rsPos_ + 2.0 < (double)total) {
        size_t i = (size_t)rsPos_;
        float t = (float)(rsPos_ - (double)i);
        for (int c = 0; c < 2; c++) {
            float y0 = buf[(i - 1) * 2 + c], y1 = buf[i * 2 + c], y2 = buf[(i + 1) * 2 + c], y3 = buf[(i + 2) * 2 + c];
            float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out.push_back(((c3 * t + c2) * t + c1) * t + c0);
        }
        rsPos_ += step;
    }
    // guarda os últimos H quadros e reposiciona o cursor
    size_t keepFrom = total - H;
    rsHist_.assign(buf.begin() + (ptrdiff_t)(keepFrom * 2), buf.end());
    rsPos_ -= (double)keepFrom;
}

void AudioCapture::HandlePacket(const BYTE* data, UINT32 frames, DWORD flags, UINT64 qpc100ns) {
    int64_t nowWall = WallClockUs();
    lastDataWallUs_ = nowWall;
    // hora (relógio do sistema) do primeiro quadro deste pacote
    int64_t wall;
    if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) && qpc100ns != 0) {
        wall = clock_.QpcToWall(Qpc100nsToUs(qpc100ns));
        // proteção: timestamp absurdo (mais de 1 s de diferença) -> estimativa
        if (std::llabs(wall - nowWall) > 1000000) wall = nowWall - (int64_t)frames * 1000000 / std::max(1, srcRate_);
    } else {
        wall = nowWall - (int64_t)frames * 1000000 / std::max(1, srcRate_);
    }

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        tmpA_.assign((size_t)frames * 2, 0.0f);
    } else {
        ToStereoFloat(data, frames, tmpA_);
    }
    // medidor de nível (pico)
    float pk = 0;
    for (float v : tmpA_) pk = std::max(pk, std::fabs(v));
    if (muted_.load()) pk = 0.0f;
    float old = peak_.load();
    while (pk > old && !peak_.compare_exchange_weak(old, pk)) {}

    if (srcRate_ == kAudioRate) {
        Deliver(tmpA_.data(), frames, wall);
    } else {
        std::vector<float> res;
        double off = 0;
        Resample(tmpA_, res, off);
        Deliver(res.data(), res.size() / 2, wall + (int64_t)(off * 1000000.0 / srcRate_));
    }
}

void AudioCapture::GenerateTestTone() {
    int64_t now = WallClockUs();
    // gera até "agora - 20 ms", em blocos de 10 ms, como uma placa de som faria
    std::vector<float> buf;
    while (testToneNextUs_ + 10000 <= now - 20000) {
        size_t n = 480;
        buf.resize(n * 2);
        float pk = 0;
        for (size_t i = 0; i < n; i++) {
            int64_t t = testToneNextUs_ + (int64_t)i * 1000000 / kAudioRate;
            int64_t ms = (t / 1000) % 1000;
            float v = ms < 50 ? 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * (float)((t % 1000000) / 1e6)) : 0.0f;
            buf[i * 2] = buf[i * 2 + 1] = v;
            pk = std::max(pk, std::fabs(v));
        }
        if (muted_.load()) pk = 0.0f;
        float old = peak_.load();
        while (pk > old && !peak_.compare_exchange_weak(old, pk)) {}
        Deliver(buf.data(), n, testToneNextUs_);
        testToneNextUs_ += 10000;
    }
    lastDataWallUs_ = now;
    FlushFile(false);
}

// ---------------------------------------------------------------------------
// Gravação alinhada ao relógio
// ---------------------------------------------------------------------------
bool AudioCapture::BeginSegment(const std::wstring& path, int64_t segStartWallUs, std::string* error) {
    std::lock_guard<std::mutex> lk(segMtx_);
    if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
    file_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        if (error) *error = "Não foi possível criar o arquivo de áudio temporário: " + LastErrorText();
        return false;
    }
    segStartUs_ = segStartWallUs;
    written_ = 0;
    fileBuf_.clear();
    lastFlushUs_ = WallClockUs();
    return true;
}

int64_t AudioCapture::EndSegment() {
    std::lock_guard<std::mutex> lk(segMtx_);
    if (file_ == INVALID_HANDLE_VALUE) return 0;
    if (!fileBuf_.empty()) {
        DWORD wr;
        WriteFile(file_, fileBuf_.data(), (DWORD)fileBuf_.size(), &wr, nullptr);
        fileBuf_.clear();
    }
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    int64_t n = written_;
    written_ = 0;
    return n;
}

void AudioCapture::WriteSamples(const float* data, size_t frames) {
    // float -> 16 bits (metade do espaço em disco; qualidade de CD é de sobra
    // para o arquivo final em AAC/MP3/PCM 16)
    size_t n = frames * kAudioChannels;
    pcm16_.resize(n);
    for (size_t i = 0; i < n; i++) {
        float v = data[i] * 32767.0f;
        v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
        pcm16_[i] = (int16_t)lrintf(v);
    }
    const char* p = reinterpret_cast<const char*>(pcm16_.data());
    fileBuf_.insert(fileBuf_.end(), p, p + n * sizeof(int16_t));
    written_ += (int64_t)frames;
    if (fileBuf_.size() >= (1u << 18)) FlushFile(true);
}

void AudioCapture::WriteZeros(size_t frames) {
    fileBuf_.insert(fileBuf_.end(), frames * kTempBytesPerFrame, 0);
    written_ += (int64_t)frames;
    if (fileBuf_.size() >= (1u << 18)) FlushFile(true);
}

void AudioCapture::FlushFile(bool force) {
    // chamado com ou sem segMtx_: quando vem de fora (thread), pego o lock
    auto doFlush = [this] {
        if (file_ != INVALID_HANDLE_VALUE && !fileBuf_.empty()) {
            DWORD wr;
            if (!WriteFile(file_, fileBuf_.data(), (DWORD)fileBuf_.size(), &wr, nullptr))
                Log::Error("Falha ao gravar áudio temporário: %s", LastErrorText().c_str());
            fileBuf_.clear();
        }
        lastFlushUs_ = WallClockUs();
    };
    if (force) { doFlush(); return; }
    std::lock_guard<std::mutex> lk(segMtx_);
    if (WallClockUs() - lastFlushUs_ > 500000) doFlush();
}

void AudioCapture::Deliver(const float* st, size_t frames, int64_t wallUs) {
    std::lock_guard<std::mutex> lk(segMtx_);
    if (file_ == INVALID_HANDLE_VALUE || frames == 0) return;
    // Onde este bloco DEVERIA começar no arquivo, segundo o relógio:
    int64_t ideal = (int64_t)std::llround((double)(wallUs - segStartUs_) * kAudioRate / 1e6);
    if (ideal + (int64_t)frames <= 0) return;                  // todo antes do início do trecho
    const int64_t tol = kAudioRate / 50;                       // 20 ms de folga (tremidas normais)
    int64_t delta = ideal - written_;
    if (written_ == 0) {
        // início do trecho: posiciona exatamente
        if (ideal > 0) WriteZeros((size_t)ideal);
        else { st += (size_t)(-ideal) * 2; frames -= (size_t)(-ideal); }
    } else if (delta > tol) {
        WriteZeros((size_t)delta);                              // buraco (silêncio, travada) -> zeros
    } else if (delta < -tol) {
        size_t drop = (size_t)std::min<int64_t>(-delta, (int64_t)frames);   // adiantado -> descarta a sobra
        st += drop * 2;
        frames -= drop;
    }
    if (!frames) return;
    // mudo com rampa curta: o ganho anda até 0 (ou volta a 1) em ~10 ms
    float target = muted_.load() ? 0.0f : 1.0f;
    if (target == 1.0f && gain_ == 1.0f) {
        WriteSamples(st, frames);
        return;
    }
    const float step = 1.0f / (kAudioRate / 100.0f);
    rampBuf_.assign(st, st + frames * kAudioChannels);
    for (size_t i = 0; i < frames; i++) {
        if (gain_ < target) gain_ = std::min(target, gain_ + step);
        else if (gain_ > target) gain_ = std::max(target, gain_ - step);
        rampBuf_[i * 2] *= gain_;
        rampBuf_[i * 2 + 1] *= gain_;
    }
    WriteSamples(rampBuf_.data(), frames);
}

void AudioCapture::PadSilence(int64_t nowWall) {
    // Sem pacotes há mais de 150 ms (nada tocando ou dispositivo fora):
    // completa com silêncio até 'agora - 150 ms' para o arquivo acompanhar o relógio.
    if (nowWall - lastDataWallUs_ < 150000) return;
    std::lock_guard<std::mutex> lk(segMtx_);
    if (file_ == INVALID_HANDLE_VALUE) return;
    int64_t target = (int64_t)((double)(nowWall - 150000 - segStartUs_) * kAudioRate / 1e6);
    if (target > written_) WriteZeros((size_t)(target - written_));
}
