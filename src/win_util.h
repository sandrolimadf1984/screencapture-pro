#pragma once
#include "common.h"

// Ajudantes do Windows usados em vários lugares.

// Escolher pasta (janela padrão do Windows). Devolve false se cancelar.
bool PickFolderDialog(HWND owner, const std::wstring& initialDir, std::wstring& outDir);
// Abrir pasta no Explorer / mostrar arquivo selecionado / abrir com o app padrão
void OpenFolderInExplorer(const std::wstring& dir);
void RevealFileInExplorer(const std::wstring& file);
bool OpenWithDefaultApp(const std::wstring& file);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);
// Manda para a Lixeira (em pendrive o Windows exclui direto)
bool MoveToRecycleBin(const std::wstring& file, std::string* error);
bool GetDiskSpace(const std::wstring& anyPathOnDrive, uint64_t& freeBytes, uint64_t& totalBytes);
std::wstring DriveLabel(const std::wstring& path);   // "D:" ou "\\servidor\pasta"
std::wstring FileSystemName(const std::wstring& path); // "NTFS", "exFAT", "FAT32"... (vazio se não souber)
// Soma do tamanho dos arquivos de uma pasta (sem subpastas). Abre cada arquivo
// para ler o tamanho real, mesmo dos que ainda estão sendo gravados.
uint64_t DirectoryBytes(const std::wstring& dir);

// A tela do Windows está acessível para captura? Fica indisponível com a sessão
// bloqueada (Win+L), na tela do Ctrl+Alt+Del e nas janelas de permissão do UAC
// (a "área de trabalho segura"): nesses momentos nenhum programa consegue gravar.
bool IsInputDesktopAvailable();

// Visual das janelas (Windows 10/11)
void SetDarkTitleBar(HWND hwnd, bool dark = true);
void SetWindowCorners(HWND hwnd, int preference);   // 0 padrão, 1 sem arredondar, 2 redondo, 3 redondo pequeno

// Exclui a janela da captura de tela (SetWindowDisplayAffinity).
// Precisa do Windows 10 versão 2004 (build 19041) ou mais novo.
bool ExcludeWindowFromCapture(HWND hwnd);
void IncludeWindowInCapture(HWND hwnd);              // desfaz a exclusão
bool IsCaptureExclusionSupported();

// Versão do Windows (build real, via RtlGetVersion)
struct WinVersion { DWORD major = 0, minor = 0, build = 0; };
WinVersion GetWindowsVersion();
std::string WindowsVersionText();

// DPI
UINT DpiForWindow(HWND hwnd);
UINT DpiForMonitor(HMONITOR mon);

// Caixas de mensagem nativas em português (usadas antes da interface existir)
void FatalMessage(const std::string& title, const std::string& text);
