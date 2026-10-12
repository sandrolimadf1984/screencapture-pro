#pragma once
#include "common.h"

// Informações de cada monitor, em PIXELS FÍSICOS (o programa é "per-monitor
// DPI aware", então não há a "mentira" do escalonamento do Windows aqui).
struct MonitorInfo {
    int number = 0;                 // 1, 2, 3... (ordem da esquerda para a direita)
    std::wstring deviceName;        // \\.\DISPLAY1
    std::string friendlyName;       // "DELL U2719D" (quando o Windows informa)
    HMONITOR handle = nullptr;
    RECT rect{};                    // área completa, coordenadas da área de trabalho virtual
    RECT work{};                    // área sem a barra de tarefas
    bool primary = false;
    UINT dpi = 96;
    int dxgiAdapter = -1;           // índice do adaptador (placa de vídeo) para o ddagrab
    int dxgiOutput = -1;            // índice da saída dentro do adaptador
    bool rotated = false;           // monitor girado (retrato ou invertido): o ddagrab não gira a imagem
    std::string adapterName;

    int Width() const { return rect.right - rect.left; }
    int Height() const { return rect.bottom - rect.top; }
    int ScalePercent() const { return (int)(dpi * 100 / 96); }
    std::string Title() const;      // "Monitor 1 (principal)"
};

std::vector<MonitorInfo> EnumerateMonitors();
// Índice (no vetor) do monitor que contém a maior parte do retângulo
int MonitorIndexForRect(const std::vector<MonitorInfo>& mons, const RECT& r);
int MonitorIndexForPoint(const std::vector<MonitorInfo>& mons, POINT p);

// Captura um pedaço da tela (BitBlt) já reduzido para no máximo maxW x maxH.
// Devolve pixels BGRA (topo para baixo). Usado nas miniaturas e no seletor.
bool CaptureScreenScaled(const RECT& src, int maxW, int maxH, std::vector<uint8_t>& bgra, int& outW, int& outH);
// Captura em tamanho real para um HBITMAP (seletor de área "congelado").
HBITMAP CaptureScreenBitmap(const RECT& src);
