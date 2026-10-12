"""Mede a sincronia áudio/vídeo de uma gravação de teste.

O teste pisca a tela (branco por ~100 ms) e toca um bipe no início de cada
segundo "redondo" do relógio. Aqui achamos os instantes das piscadas no vídeo e
dos bipes no áudio e mostramos a diferença (vídeo - áudio) em milissegundos.

Uso: python3 measure_sync.py <video> [fps] [recorte]
     recorte = filtro crop do ffmpeg para olhar só uma parte da imagem
               (padrão: a quinta parte da esquerda, "iw/5:ih:0:0")
"""
import struct
import subprocess
import sys

path = sys.argv[1]
fps = int(sys.argv[2]) if len(sys.argv) > 2 else 30
crop = sys.argv[3] if len(sys.argv) > 3 else "iw/5:ih:0:0"

# brilho médio de cada quadro (imagem reduzida para 32x18 em tons de cinza)
r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:v:0", "-vf",
                    "crop=%s,scale=32:18,format=gray" % crop, "-f", "rawvideo", "-"], capture_output=True)
frames = r.stdout
n = len(frames) // 576
lum = [sum(frames[i * 576:(i + 1) * 576]) / 576 for i in range(n)]
flashes = [i / fps for i in range(1, n) if lum[i] > 128 and lum[i - 1] <= 128]

# instantes em que o bipe começa (janelas de 5 ms)
r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a:0", "-ac", "1", "-ar", "48000",
                    "-f", "f32le", "-"], capture_output=True)
a = r.stdout
ns = len(a) // 4
smp = struct.unpack("<%df" % ns, a)
beeps, prev, win = [], False, 240
for i in range(0, ns - win, win):
    on = max(abs(x) for x in smp[i:i + win]) > 0.1
    if on and not prev:
        beeps.append(i / 48000)
    prev = on

print("quadros", n, "duração %.2f" % (n / fps), "áudio %.2f" % (ns / 48000))
print("piscadas", [round(x, 2) for x in flashes][:60])
print("bipes   ", [round(x, 2) for x in beeps][:60])
d = []
for f in flashes:
    if beeps:
        b = min(beeps, key=lambda x: abs(x - f))
        if abs(b - f) < 0.45:
            d.append(round((f - b) * 1000, 1))
print("diferença vídeo-áudio (ms):", d)
