#include "process.h"

#include "log.h"

// Job Object compartilhado: KILL_ON_JOB_CLOSE garante que nenhum ffmpeg
// sobreviva ao programa.
static HANDLE GetJob() {
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return j;
    }();
    return job;
}

std::wstring QuoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    // regra do CommandLineToArgvW: barras antes de aspas precisam ser dobradas
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { backslashes++; continue; }
        if (c == L'"') { out.append(backslashes * 2 + 1, L'\\'); out.push_back(L'"'); }
        else { out.append(backslashes, L'\\'); out.push_back(c); }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

ChildProcess::~ChildProcess() {
    if (Running()) Kill();
    CloseStdin();
    JoinReaders();
    if (proc_) CloseHandle(proc_);
}

bool ChildProcess::Start(const std::wstring& exe, const std::wstring& args, const std::wstring& workDir, std::string* error) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 1 << 16) || !CreatePipe(&errR, &errW, &sa, 1 << 16)) {
        if (error) *error = "CreatePipe falhou: " + LastErrorText();
        return false;
    }
    // as pontas que ficam com a gente NÃO podem ser herdadas pelo filho
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = QuoteArg(exe) + L" " + args;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = inR;
    si.StartupInfo.hStdOutput = outW;
    si.StartupInfo.hStdError = errW;

    // Lista explícita de handles herdáveis: sem isso, dois ffmpeg rodando ao
    // mesmo tempo herdariam os pipes um do outro e os pipes nunca fechariam.
    HANDLE inherit[3] = {inR, outW, errW};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<uint8_t> attrBuf(attrSize ? attrSize : 64);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool useAttrs = attrSize && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
    if (useAttrs) si.lpAttributeList = attrs;

    PROCESS_INFORMATION pi = {};
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | (useAttrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, flags, nullptr,
                             workDir.empty() ? nullptr : workDir.c_str(), &si.StartupInfo, &pi);
    if (!ok && useAttrs) {
        // alguns ambientes não aceitam a lista de handles — tenta do jeito simples
        si.lpAttributeList = nullptr;
        ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                            nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si.StartupInfo, &pi);
    }
    DWORD createErr = GetLastError();
    if (useAttrs) DeleteProcThreadAttributeList(attrs);
    CloseHandle(inR);
    CloseHandle(outW);
    CloseHandle(errW);
    if (!ok) {
        CloseHandle(inW);
        CloseHandle(outR);
        CloseHandle(errR);
        if (error) *error = "Não foi possível iniciar " + Narrow(FileNameOf(exe)) + ": " + LastErrorText(createErr);
        return false;
    }
    if (GetJob()) AssignProcessToJobObject(GetJob(), pi.hProcess);
    CloseHandle(pi.hThread);
    proc_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    stdinW_ = inW;
    stdoutR_ = outR;
    stderrR_ = errR;
    outThread_ = std::thread([this] { ReaderLoop(stdoutR_, &onOut_); });
    errThread_ = std::thread([this] { ReaderLoop(stderrR_, &onErr_); });
    return true;
}

void ChildProcess::ReaderLoop(HANDLE pipe, LineCallback* cb) {
    std::string pending;
    char buf[4096];
    for (;;) {
        DWORD rd = 0;
        if (!ReadFile(pipe, buf, sizeof(buf), &rd, nullptr) || rd == 0) break;
        pending.append(buf, rd);
        size_t pos;
        // o ffmpeg separa algumas linhas com \r (barra de progresso) — trato os dois
        while ((pos = pending.find_first_of("\r\n")) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && *cb) (*cb)(line);
        }
        if (pending.size() > 1 << 20) pending.clear();   // proteção contra lixo sem quebra de linha
    }
    if (!pending.empty() && *cb) (*cb)(pending);
}

bool ChildProcess::WriteStdin(const std::string& data) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!stdinW_) return false;
    DWORD wr = 0;
    return WriteFile(stdinW_, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
}

void ChildProcess::CloseStdin() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (stdinW_) { CloseHandle(stdinW_); stdinW_ = nullptr; }
}

bool ChildProcess::Running() const { return proc_ && WaitForSingleObject(proc_, 0) == WAIT_TIMEOUT; }

bool ChildProcess::Wait(DWORD ms) { return !proc_ || WaitForSingleObject(proc_, ms) == WAIT_OBJECT_0; }

DWORD ChildProcess::ExitCode() const {
    DWORD c = (DWORD)-1;
    if (proc_) GetExitCodeProcess(proc_, &c);
    return c;
}

void ChildProcess::Kill() {
    if (proc_ && Running()) {
        TerminateProcess(proc_, 0xDEAD);
        WaitForSingleObject(proc_, 3000);
    }
}

double ChildProcess::CpuSeconds() const {
    FILETIME c, e, k, u;
    if (!proc_ || !GetProcessTimes(proc_, &c, &e, &k, &u)) return 0;
    auto toS = [](const FILETIME& f) { return (double)(((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime) / 1e7; };
    return toS(k) + toS(u);
}

void ChildProcess::JoinReaders() {
    // Se o processo já morreu, os pipes fecham e as threads saem sozinhas.
    if (outThread_.joinable()) outThread_.join();
    if (errThread_.joinable()) errThread_.join();
    if (stdoutR_) { CloseHandle(stdoutR_); stdoutR_ = nullptr; }
    if (stderrR_) { CloseHandle(stderrR_); stderrR_ = nullptr; }
}

std::string RunCapture(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs, int* exitCode, std::string* stdoutOnly) {
    ChildProcess p;
    std::mutex m;
    std::string all, out;
    p.OnStdout([&](const std::string& l) { std::lock_guard<std::mutex> lk(m); all += l + "\n"; out += l + "\n"; });
    p.OnStderr([&](const std::string& l) { std::lock_guard<std::mutex> lk(m); all += l + "\n"; });
    std::string err;
    if (!p.Start(exe, args, L"", &err)) {
        if (exitCode) *exitCode = -2;
        return err;
    }
    p.CloseStdin();
    bool done = p.Wait(timeoutMs);
    if (!done) p.Kill();
    p.JoinReaders();
    if (exitCode) *exitCode = done ? (int)p.ExitCode() : -1;
    if (stdoutOnly) *stdoutOnly = out;
    return all;
}
