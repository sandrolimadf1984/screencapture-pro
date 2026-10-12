<p align="center">
  <img src="assets/app_256.png" width="96" alt="Ícone do ScreenCapture Pro">
</p>

<h1 align="center">ScreenCapture Pro</h1>

<p align="center">
  Gravador de tela portátil para Windows 10 e 11, com som do computador, microfone e pausa.<br>
  Não precisa instalar, não pede administrador e não usa a internet.
</p>

<p align="center">
  <a href="https://github.com/sandrolimadf1984/screencapture-pro/actions/workflows/build.yml"><img src="https://github.com/sandrolimadf1984/screencapture-pro/actions/workflows/build.yml/badge.svg" alt="Compilação"></a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6" alt="Windows 10 e 11">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17">
  <a href="LICENSE"><img src="https://img.shields.io/badge/licen%C3%A7a-MIT-green" alt="Licença MIT"></a>
</p>

<p align="center">
  <a href="https://github.com/sandrolimadf1984/screencapture-pro/releases/latest"><b>Baixar a versão mais recente</b></a>
  &nbsp;·&nbsp; <a href="docs/TUTORIAL.md">Tutorial</a>
  &nbsp;·&nbsp; <a href="README.en.md">English</a>
</p>

<p align="center">
  <img src="docs/images/home.png" width="45%" alt="Tela principal">
  &nbsp;
  <img src="docs/images/cfg_audio.png" width="45%" alt="Configurações de áudio com a mistura de volumes">
</p>
<p align="center">
  <img src="docs/images/bar_rec.png" width="70%" alt="Barra de controle durante a gravação">
</p>

## O que ele faz

- **Tela inteira ou só uma área.** Escolha o monitor, desenhe um retângulo ou clique numa janela.
- **Pausar e continuar** quantas vezes quiser: no fim, sai um arquivo só.
- **Barra de controle invisível no vídeo.** Você vê, quem assiste não.
- **Som do computador e microfone**, com escolha do aparelho, medidores ao vivo e mistura pronta: Equilibrado, Comentar vídeo e Destacar o PC (volumes de 0 a 300%, com limitador).
- **Botão Mic** (ou F8) para silenciar só a sua voz no meio da gravação, sem cortar o vídeo.
- **MP4** (H.264 ou H.265 + AAC) ou **AVI** de verdade (MPEG-4/Xvid, Motion JPEG ou H.264 + MP3/PCM).
- **Usa a placa de vídeo** quando existe (NVIDIA NVENC, Intel Quick Sync ou AMD AMF) e volta sozinho para o processador se precisar.
- Resoluções de 360p a 4K, mais uma personalizada ("2060p"), e 15, 24, 30 ou 60 FPS.
- **Atalhos globais:** F9 gravar, F10 pausar, F11 finalizar e F8 microfone.
- **Sem limite de tempo:** grava até você pausar ou finalizar. Durante a gravação o PC não suspende e a tela não apaga sozinha.
- **Tela bloqueada não estraga a gravação:** com Win+L, Ctrl+Alt+Del ou uma janela de permissão do Windows, a gravação pausa sozinha e continua quando a tela volta.
- **Recuperação:** se o PC desligar no meio, a gravação é recuperada na próxima vez que o programa abrir.
- **Portátil e privado:** roda até de um pendrive, não envia nada para a internet e não coleta dados.
- Interface em português, com tema escuro e uma lista para gerenciar as gravações.

## Como usar

1. Baixe o **ScreenCapturePro_Portable.zip** na [página de versões](https://github.com/sandrolimadf1984/screencapture-pro/releases/latest).
2. Clique com o botão direito no arquivo e escolha **Extrair tudo...**.
3. Abra a pasta e dê dois cliques em **ScreenCapturePro.exe**.
4. Clique em **Gravar** e escolha **Gravar tela inteira** ou **Gravação livre**.

Na primeira vez, o Windows pode mostrar "O Windows protegeu o computador", porque o programa não tem assinatura digital. Clique em **Mais informações** e depois em **Executar assim mesmo**.

O passo a passo completo, com imagens e a melhor configuração para gravar, está no **[tutorial](docs/TUTORIAL.md)**.

## Como funciona

- **C++17 nativo (Win32)**, sem .NET e sem instalador. O `.exe` tem cerca de 3 MB.
- **Interface** em Direct3D 11 + [Dear ImGui](https://github.com/ocornut/imgui). O seletor de área, a barra e a moldura usam Direct2D/DirectWrite.
- **Vídeo** pelo FFmpeg 7.1, rodando como processo filho dentro de um Job Object. A captura principal é a DXGI Desktop Duplication (`ddagrab`), com GDI (`gdigrab`) de reserva.
- **Áudio** pelo WASAPI dentro do programa (loopback + microfone), convertido para 48 kHz e alinhado ao relógio do sistema.
- **Pausa sem perder a sincronia:** cada trecho entre pausas vira um arquivo `.nut`. No final, o vídeo é juntado sem recodificar e o áudio é recortado exatamente no mesmo intervalo.
- **Fora da gravação:** `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` tira a barra e as janelas do programa da captura, com plano B automático em Windows antigos.

Detalhes, diagrama e mapa dos arquivos em [docs/ARQUITETURA.md](docs/ARQUITETURA.md).

## Compilar

No Windows, com o [MSYS2](https://www.msys2.org) (terminal "MSYS2 MINGW64"):

```bash
pacman -S --needed mingw-w64-x86_64-gcc
CXX=g++ WINDRES=windres bash build.sh
```

No Linux (Ubuntu/Debian), por compilação cruzada:

```bash
sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
bash build.sh
```

O programa sai em `build/ScreenCapturePro.exe` e precisa de `ffmpeg\ffmpeg.exe` na mesma pasta (o pacote portátil já tem). A cada envio para o GitHub, o **GitHub Actions** compila o `.exe` automaticamente. Mais detalhes em [docs/COMPILAR.md](docs/COMPILAR.md).

## Testes

- **Ponta a ponta no Wine** (Xvfb + PulseAudio) com o FFmpeg real: o script abre o programa, grava, pausa, silencia o microfone, finaliza e confere cada vídeo com `ffprobe` e decodificação completa ([`tools/wine_e2e.py`](tools/wine_e2e.py)).
- **Sincronia de áudio e vídeo** medida com piscadas na tela e bipes de teste: diferença de até cerca de 45 ms, sem aumentar com o tempo nem depois das pausas.
- **Gravação longa:** uma sessão de 3h30 foi montada com vídeo e áudio da mesma duração e sincronia exata no começo e depois de 3h20.
- **Tela bloqueada** (simulada no teste): a gravação pausa sozinha e continua quando a tela volta, num arquivo só.
- A captura DXGI, o loopback do WASAPI, a exclusão da barra e os codificadores de placa de vídeo dependem de recursos que o Wine não tem. Eles seguem a documentação da Microsoft e do FFmpeg e têm plano B automático.

## Estrutura do projeto

```
src/           código do programa (C++17)
third_party/   Dear ImGui e nlohmann/json
assets/        ícones e fontes
tools/         testes automatizados e gerador de ícones
docs/          tutorial, arquitetura, compilação e imagens
build.sh       script de compilação
```

## Privacidade

O programa não se conecta à internet: não envia vídeos, imagens, configurações nem dados de uso. Na gravação livre, só a área escolhida é capturada.

## Licença e créditos

O código deste repositório está sob a [licença MIT](LICENSE). Os componentes de terceiros mantêm as próprias licenças:

- [Dear ImGui](https://github.com/ocornut/imgui), MIT
- [JSON for Modern C++ (nlohmann/json)](https://github.com/nlohmann/json), MIT
- [Noto Sans](https://fonts.google.com/noto), SIL Open Font License 1.1
- [FFmpeg](https://ffmpeg.org), GPL v3. Não faz parte deste repositório. O pacote portátil da [página de versões](https://github.com/sandrolimadf1984/screencapture-pro/releases/latest) traz o `ffmpeg.exe` original como programa separado, com a licença e a origem do código-fonte na pasta `licencas`.

Versão atual: **1.1.1**. Veja o [histórico de mudanças](CHANGELOG.md).

## Autor

**Sandro de Lima Pereira**: [GitHub](https://github.com/sandrolimadf1984) · [LinkedIn](https://www.linkedin.com/in/sandrolimadev)
