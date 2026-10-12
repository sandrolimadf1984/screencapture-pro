#pragma once
#include "common.h"

// Processo filho (ffmpeg.exe) com stdin/stdout/stderr redirecionados.
// Cada linha de saída chega por callback, numa thread própria de leitura.
// Todos os filhos ficam num "Job Object": se o programa fechar de repente
// (travamento, Gerenciador de Tarefas), o Windows mata o ffmpeg junto — nada
// fica gravando escondido.
class ChildProcess {
public:
    using LineCallback = std::function<void(const std::string& line)>;

    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    void OnStdout(LineCallback cb) { onOut_ = std::move(cb); }
    void OnStderr(LineCallback cb) { onErr_ = std::move(cb); }

    bool Start(const std::wstring& exe, const std::wstring& args, const std::wstring& workDir, std::string* error);
    bool WriteStdin(const std::string& data);
    void CloseStdin();
    bool Running() const;
    bool Wait(DWORD ms);              // true = terminou
    DWORD ExitCode() const;
    void Kill();
    HANDLE Handle() const { return proc_; }
    DWORD Pid() const { return pid_; }
    // Tempo de CPU usado pelo processo (usuário + kernel), em segundos
    double CpuSeconds() const;
    void JoinReaders();               // espera as threads de leitura acabarem

private:
    void ReaderLoop(HANDLE pipe, LineCallback* cb);
    HANDLE proc_ = nullptr;
    DWORD pid_ = 0;
    HANDLE stdinW_ = nullptr, stdoutR_ = nullptr, stderrR_ = nullptr;
    std::thread outThread_, errThread_;
    LineCallback onOut_, onErr_;
    mutable std::mutex mtx_;
};

// Atalho: roda um comando, espera terminar e devolve stdout+stderr juntos.
// timeoutMs estourado = processo é morto e exitCode fica -1.
std::string RunCapture(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs, int* exitCode,
                       std::string* stdoutOnly = nullptr);

// Coloca um argumento entre aspas do jeito que o Windows entende
std::wstring QuoteArg(const std::wstring& a);
