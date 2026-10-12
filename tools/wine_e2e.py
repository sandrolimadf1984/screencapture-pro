"""Teste ponta a ponta no Wine: grava com várias configurações e confere o arquivo.

Uso: python3 wine_e2e.py <pasta_teste> <exe> <nome_config> [<json com overrides>]

Precisa de: Wine 64 bits (WINEPREFIX abaixo), Xvfb no DISPLAY :60, xdotool,
ImageMagick (import), ffmpeg/ffprobe do Linux. A pasta de teste deve ter a
mesma estrutura do pacote portátil (ffmpeg\\ffmpeg.exe, config\\portable.ini).

Roteiro: abre o programa, aperta F9 (gravação livre), desenha uma área,
confirma com Enter, pausa e continua com F10, finaliza com F11 e mede a
sincronia entre as piscadas da tela e os bipes do tom de teste.
"""
import glob
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
T = sys.argv[1]
EXE = sys.argv[2]
NAME = sys.argv[3]
OVR = json.loads(sys.argv[4]) if len(sys.argv) > 4 else {}
S = os.environ.get("SCRATCH", "/tmp")
env = dict(os.environ, WINEPREFIX=os.environ.get("WINEPREFIX", os.path.expanduser("~/.wine-scp")), WINEARCH="win64", DISPLAY=":60",
           WINEDEBUG="-all", SCP_TEST_TONE=os.environ.get("E2E_TONE", "1"), SCP_FORCE_BAR="1")
DURING = os.environ.get("E2E_DURING", "")   # comando extra durante a gravação (ex.: tocar som)
MIC_KEY = os.environ.get("E2E_MIC_KEY", "")  # ex.: "F8" -> silencia e reativa o microfone durante o teste
# E2E_LOCK_TEST=1 -> simula a tela do Windows bloqueada no meio da gravação (o ffmpeg cai
# e a "tela" fica indisponível por alguns segundos); a gravação deve pausar e voltar sozinha
LOCK_TEST = os.environ.get("E2E_LOCK_TEST", "") == "1"
LOCK_FILE = os.path.join(T, "tela_bloqueada.flag")
if LOCK_TEST:
    env["SCP_TEST_LOCKFILE"] = "Z:" + os.path.abspath(LOCK_FILE).replace("/", "\\")


def sh(*a, **k):
    return subprocess.run(list(a), env=env, **k)


def xdo(*a):
    sh("xdotool", *a)


sh("wineserver", "-k", stderr=subprocess.DEVNULL)
time.sleep(1)
for d in ("logs", "recordings"):
    for f in glob.glob(os.path.join(T, d, "*")):
        if os.path.isfile(f):
            os.remove(f)
# configurações
base = {
    "version": 1,
    "video": {"container": 0, "codecMp4": 0, "codecAvi": 2, "encoder": 0, "quality": 0, "customBitrateKbps": 8000,
              "resolution": 0, "customW": 3662, "customH": 2060, "aspect": 0, "fps": 30, "drawMouse": True, "captureMethod": 0},
    "audio": {"system": True, "systemDeviceId": "", "mic": False, "micDeviceId": "", "systemVolume": 100, "micVolume": 100,
              "tracks": 0, "aacBitrate": 160, "aviAudio": 0, "mp3Bitrate": 192},
    "recordings": {"outputDir": "", "namePattern": "Teste_" + NAME + "_{hora}", "afterFinish": 4, "countdown": 0,
                   "minimizeOnRecord": True, "showRegionBorder": True},
    "hotkeys": {"start": {"enabled": True, "mods": 0, "vk": 120}, "pause": {"enabled": True, "mods": 0, "vk": 121},
                "stop": {"enabled": True, "mods": 0, "vk": 122}, "startAction": 2},
}
for sec, vals in OVR.items():
    base[sec].update(vals)
os.makedirs(os.path.join(T, "config"), exist_ok=True)
json.dump(base, open(os.path.join(T, "config", "settings.json"), "w"), indent=1)

app = subprocess.Popen(["wine", EXE], cwd=T, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(8)
xdo("key", "F9")                     # atalho global -> seleção de área
time.sleep(2.5)
xdo("mousemove", "300", "200", "mousedown", "1")
time.sleep(0.2)
xdo("mousemove", "700", "450")
time.sleep(0.2)
xdo("mousemove", "1101", "701")      # área com tamanho ímpar: o programa ajusta para par
time.sleep(0.2)
xdo("mouseup", "1")
time.sleep(0.6)
xdo("key", "Return")                 # confirma pelo teclado
flasher = subprocess.Popen(["bash", os.path.join(HERE, "flasher.sh"), "16"], env=env)
during = subprocess.Popen(DURING, shell=True) if DURING else None
time.sleep(3)
sh("import", "-window", "root", os.path.join(S, "e2e_%s.png" % NAME))
if MIC_KEY:
    xdo("key", MIC_KEY)              # microfone mudo...
    time.sleep(1.5)
    xdo("key", MIC_KEY)              # ...e de volta
    time.sleep(0.5)
else:
    time.sleep(2)
if LOCK_TEST:
    open(LOCK_FILE, "w").close()     # "bloqueia" a tela...
    pids = subprocess.run(["pgrep", "-f", r"ffmpeg\.exe"], capture_output=True, text=True).stdout.split()
    for pid in pids:
        if int(pid) != os.getpid():
            subprocess.run(["kill", "-9", pid])   # ...e a captura cai, como acontece no Windows
    time.sleep(4)
    sh("import", "-window", "root", os.path.join(S, "e2e_%s_bloqueada.png" % NAME))
    os.remove(LOCK_FILE)             # "desbloqueia"
    time.sleep(5)
xdo("key", "F10")                    # pausa...
time.sleep(1.2)
xdo("key", "F10")                    # ...e continua
time.sleep(4)
xdo("key", "F11")                    # finaliza
log = ""
for _ in range(90):
    time.sleep(1)
    logs = glob.glob(os.path.join(T, "logs", "*.log"))
    log = open(logs[0], encoding="utf-8", errors="replace").read() if logs else ""
    if "Vídeo salvo" in log or "Falha ao montar" in log or "FALHA GRAVE" in log:
        break
time.sleep(2)                        # deixa o programa terminar a limpeza dos temporários
flasher.kill()
if during:
    during.kill()
sh("wineserver", "-k", stderr=subprocess.DEVNULL)
outs = sorted(glob.glob(os.path.join(T, "recordings", "Teste_*")))
print("=== %s ===" % NAME)
for line in log.splitlines():
    if any(k in line for k in ("Nova gravação", "Vídeo salvo", "ERRO", "Tentativa", "Trecho seg", "Áudio", "Microfone ", "tela do Windows", "Gravação pausada")) and "Affinity" not in line:
        print("   ", line[:230])
if not outs:
    print("   SEM ARQUIVO DE SAÍDA")
    sys.exit(1)
out = outs[-1]
r = subprocess.run(["ffprobe", "-v", "error", "-show_entries",
                    "stream=codec_name,codec_tag_string,width,height,r_frame_rate,nb_frames,duration,sample_rate,channels:format=format_name,duration",
                    "-of", "compact", out], capture_output=True, text=True)
print("   " + r.stdout.strip().replace("\n", "\n   "))
dec = subprocess.run(["ffmpeg", "-v", "error", "-i", out, "-f", "null", "-"], capture_output=True, text=True)
print("   decodificação:", "OK" if dec.returncode == 0 and not dec.stderr.strip() else "PROBLEMAS: " + dec.stderr[:300])
m = subprocess.run(["python3", os.path.join(HERE, "measure_sync.py"), out, str(base["video"]["fps"])], capture_output=True, text=True)
print("   " + m.stdout.strip().replace("\n", "\n   "))
tmp = glob.glob(os.path.join(T, "recordings", ".screencapture_tmp", "*"))
print("   temporários restantes:", len(tmp))
