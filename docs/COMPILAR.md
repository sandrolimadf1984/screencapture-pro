# Como compilar o ScreenCapture Pro

O resultado é um único arquivo, `build/ScreenCapturePro.exe` (Windows 10/11, 64 bits). Para ele funcionar, coloque-o numa pasta que tenha `ffmpeg\ffmpeg.exe`. O jeito mais fácil é copiar o `.exe` novo para dentro da pasta do pacote portátil (da página de versões), substituindo o antigo.

## No Windows (MSYS2)

1. Instale o MSYS2 pelo site [msys2.org](https://www.msys2.org) (instalação padrão).
2. Abra o terminal **MSYS2 MINGW64** e instale o compilador:

   ```bash
   pacman -S --needed mingw-w64-x86_64-gcc
   ```

3. Entre na pasta do código. Exemplo, se ela estiver em Documentos:

   ```bash
   cd "$(cygpath "$USERPROFILE")/Documents/screencapture-pro"
   ```

4. Compile:

   ```bash
   CXX=g++ WINDRES=windres bash build.sh
   ```

5. O programa sai em `build\ScreenCapturePro.exe`.

## No Linux (compilação cruzada)

Foi assim que as versões publicadas foram geradas (Ubuntu 24.04):

```bash
sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
bash build.sh          # versão final, em build/
bash build.sh debug    # com console de depuração, em build_debug/
```

## No GitHub (automático)

A cada envio para a branch `main`, o GitHub Actions compila o programa ([`.github/workflows/build.yml`](../.github/workflows/build.yml)). O `.exe` fica na aba **Actions**, dentro da execução, em **Artifacts**.

## Testes automatizados (Linux + Wine)

A pasta `tools` tem os testes usados durante o desenvolvimento:

| Arquivo | O que faz |
| --- | --- |
| `wine_e2e.py` | Abre o programa no Wine, grava uma área, pausa, continua, silencia o microfone e finaliza. Depois confere o arquivo com `ffprobe` e decodificação completa |
| `flasher.sh` | Pisca a tela no início de cada segundo, para o teste de sincronia |
| `measure_sync.py` | Mede a diferença entre as piscadas no vídeo e os bipes no áudio |
| `make_assets.py` | Gera os ícones do programa (precisa do Pillow) |

Para o teste de sincronia, a variável de ambiente `SCP_TEST_TONE=1` faz o programa gravar um bipe por segundo no lugar do som do PC.

Com `E2E_LOCK_TEST=1`, o `wine_e2e.py` simula a tela do Windows bloqueada no meio da gravação: o programa recebe `SCP_TEST_LOCKFILE` e trata a tela como bloqueada enquanto esse arquivo existir. A gravação deve pausar e continuar sozinha.

Precisa de: Wine 64 bits, Xvfb no `DISPLAY :60`, PulseAudio, xdotool, ImageMagick e ffmpeg/ffprobe do Linux. A pasta de teste deve ter a mesma estrutura do pacote portátil (`ffmpeg\ffmpeg.exe` e `config\portable.ini`).

```bash
python3 tools/wine_e2e.py <pasta_de_teste> <caminho_do_exe> <nome_do_teste> ['{"video": {"fps": 60}}']
```

O Wine não tem a captura DXGI, o loopback do WASAPI, a exclusão da barra na captura nem os codificadores de placa de vídeo. Essas partes foram feitas seguindo a documentação da Microsoft e do FFmpeg e têm plano B automático (GDI, aviso na tela, barra fora da área e codificação no processador).
