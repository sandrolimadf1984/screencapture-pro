#!/bin/bash
# Pisca o fundo da tela do X (branco por ~100 ms) no início de cada segundo
# "redondo" do relógio, durante N segundos. Usado no teste de sincronia.
# Uso: DISPLAY=:60 ./flasher.sh 15
end=$(( $(date +%s) + ${1:-15} ))
while [ "$(date +%s)" -lt "$end" ]; do
  falta=$(python3 -c "import time,math; t=time.time(); print(math.floor(t)+1-t)")
  sleep "$falta"
  xsetroot -solid white
  sleep 0.1
  xsetroot -solid black
done
